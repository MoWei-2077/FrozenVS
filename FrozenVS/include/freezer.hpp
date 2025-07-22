#pragma once

#include "utils.hpp"
#include "vpopen.hpp"
#include "managedApp.hpp"
#include "doze.hpp"
#include "frozen.hpp"
#include "systemTools.hpp"

#define PACKET_SIZE      128
#define USER_PORT        100
#define MAX_PLOAD        125
#define MSG_LEN          125
typedef struct _user_msg_info
{
    struct nlmsghdr hdr;
    char  msg[MSG_LEN];
} user_msg_info;


class Freezer {
private:
	Frozen& frozen;
	ManagedApp& managedApp;
	SystemTools& systemTools;
	Settings& settings;
	Doze& doze;

	vector<thread> threads;

	WORK_MODE workMode = WORK_MODE::GLOBAL_SIGSTOP;
	map<int, int> pendingHandleList;     //挂起列队 无论黑白名单 { uid, timeRemain:sec }
	set<int> lastForegroundApp;          //前台应用
	set<int> curForegroundApp;           //新前台应用
	set<int> curFgBackup;                //新前台应用备份 用于进入doze前备份， 退出后恢复

	uint32_t timelineIdx = 0;
	uint32_t unfrozenTimeline[4096] = {};
	map<int, uint32_t> unfrozenIdx;

	int refreezeSecRemain = 20; //20秒进行开机冻结

	static const size_t GET_VISIBLE_BUF_SIZE = 256 * 1024;
	unique_ptr<char[]> getVisibleAppBuff;

	binder_state bs{ -1, nullptr, 128 * 1024ULL };

	const char* cgroupV2FreezerCheckPath = "/sys/fs/cgroup/uid_0/cgroup.freeze";
	const char* cgroupV2frozenCheckPath = "/sys/fs/cgroup/frozen/cgroup.freeze";       // "1" frozen
	const char* cgroupV2unfrozenCheckPath = "/sys/fs/cgroup/unfrozen/cgroup.freeze";   // "0" unfrozen

	// const char cpusetEventPath[] = "/dev/cpuset/top-app";
	const char* cpusetEventPathA12 = "/dev/cpuset/top-app/tasks";
	const char* cpusetEventPathA13 = "/dev/cpuset/top-app/cgroup.procs";

	const char* cgroupV1FrozenPath = "/dev/Frozen/frozen/cgroup.procs";
	const char* cgroupV1UnfrozenPath = "/dev/Frozen/unfrozen/cgroup.procs";

	// 如果直接使用 uid_xxx/cgroup.freeze 可能导致无法解冻
	const char* cgroupV2UidPidPath = "/sys/fs/cgroup/uid_%d/pid_%d/cgroup.freeze"; // "1"frozen   "0"unfrozen
	const char* cgroupV2FrozenPath = "/sys/fs/cgroup/frozen/cgroup.procs";         // write pid
	const char* cgroupV2UnfrozenPath = "/sys/fs/cgroup/unfrozen/cgroup.procs";     // write pid


	const char v2wchan[16] = "do_freezer_trap";      // FreezerV2冻结状态
	const char v1wchan[15] = "__refrigerator";       // FreezerV1冻结状态
	const char SIGSTOPwchan[15] = "do_signal_stop";  // SIGSTOP冻结状态
	const char v2xwchan[11] = "get_signal";          //不完整V2冻结状态
	const char epoll_wait1_wchan[15] = "SyS_epoll_wait";
	const char epoll_wait2_wchan[14] = "do_epoll_wait";
	const char binder_wchan[24] = "binder_ioctl_write_read";
	const char pipe_wchan[10] = "pipe_wait";

public:
	Freezer& operator=(Freezer&&) = delete;

	const string workModeStr(const WORK_MODE mode) {
		const string modeStrList[] = {
				"全局SIGSTOP",
				"FreezerV1 (FROZEN)",
				"FreezerV2 (UID)",
				"FreezerV2 (FROZEN)",
				"Unknown" };
		const uint32_t idx = static_cast<uint32_t>(mode);
		return modeStrList[idx <= 4 ? idx : 4];
	}

	Freezer(Frozen& frozen, Settings& settings, ManagedApp& managedApp,
		SystemTools& systemTools, Doze& doze) :
		frozen(frozen), managedApp(managedApp), systemTools(systemTools),
		settings(settings), doze(doze) {

		getVisibleAppBuff = make_unique<char[]>(GET_VISIBLE_BUF_SIZE);

		binderInit("/dev/binder");
  
		threads.emplace_back(thread(&Freezer::cpuSetTriggerTask, this));  // 多线程解冻
		threads.emplace_back(thread(&Freezer::cycleThreadFunc, this));    // 多线程冻结 
		threads.emplace_back(thread(&Freezer::ReKernelMagiskFunc, this)); // ReKernel
		threads.emplace_back(thread(&Freezer::NkBinderMagiskFunc, this)); // NkBinder

		checkAndMountV2();
		switch (static_cast<WORK_MODE>(settings.setMode)) {
		case WORK_MODE::GLOBAL_SIGSTOP: {
			workMode = WORK_MODE::GLOBAL_SIGSTOP;
			frozen.setWorkMode(workModeStr(workMode));
			frozen.log("已设置[全局SIGSTOP], [Freezer冻结]将变为[SIGSTOP冻结]");
		} return;

		case WORK_MODE::V1F: {
			if (mountFreezerV1()) {
				workMode = WORK_MODE::V1F;
				frozen.setWorkMode(workModeStr(workMode));
				frozen.log("Freezer类型已设为 V1(FROZEN)");
				return;
			}
			frozen.log("不支持自定义Freezer类型 V1(FROZEN) 失败");
		} break;

		case WORK_MODE::V2UID: {
			if (checkFreezerV2UID()) {
				workMode = WORK_MODE::V2UID;
				frozen.setWorkMode(workModeStr(workMode));
				frozen.log("Freezer类型已设为 V2(UID)");
				return;
			}
			frozen.log("不支持自定义Freezer类型 V2(UID)");
		} break;

		case WORK_MODE::V2FROZEN: {
			if (checkFreezerV2FROZEN()) {
				workMode = WORK_MODE::V2FROZEN;
				frozen.setWorkMode(workModeStr(workMode));
				frozen.log("Freezer类型已设为 V2(FROZEN)");
				return;
			}
			frozen.log("不支持自定义Freezer类型 V2(FROZEN)");
		} break;
		}

		if (checkFreezerV2FROZEN()) {
			workMode = WORK_MODE::V2FROZEN;
			frozen.log("Freezer类型已设为 V2(FROZEN)");
		}
		else if (checkFreezerV2UID()) {
			workMode = WORK_MODE::V2UID;
			frozen.log("Freezer类型已设为 V2(UID)");
		}
		else if (mountFreezerV1()) {
			workMode = WORK_MODE::V1F;
			frozen.log("Freezer类型已设为 V1(FROZEN)");
		}
		else {
			workMode = WORK_MODE::GLOBAL_SIGSTOP;
			frozen.log("不支持任何Freezer, 已开启 [全局SIGSTOP] 冻结模式");
		}
		frozen.setWorkMode(workModeStr(workMode));
	}

	bool isV1Mode() {
		return workMode == WORK_MODE::V1F;
	}

	void getPids(appInfoStruct& appInfo) {
		START_TIME_COUNT;

		appInfo.pids.clear();

		DIR* dir = opendir("/proc");
		if (dir == nullptr) {
			char errTips[256];
			snprintf(errTips, sizeof(errTips), "错误: %s() [%d]:[%s]", __FUNCTION__, errno,
				strerror(errno));
			fprintf(stderr, "%s", errTips);
			frozen.log(errTips);
			return;
		}

        char fullPath[64];
        memcpy(fullPath, "/proc/", 6);

		const string& package = appInfo.package;

		struct dirent* file;
		while ((file = readdir(dir)) != nullptr) {
			if (file->d_name[0] < '0' || file->d_name[0] > '9') continue;

			const int pid = atoi(file->d_name);
			if (pid <= 100) continue;

			memcpy(fullPath + 6, file->d_name, 6);

			struct stat statBuf;
			if (stat(fullPath, &statBuf) || statBuf.st_uid != (uid_t)appInfo.uid)continue;

			strcat(fullPath + 8, "/cmdline");

			char readBuff[256];
			if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0) continue;

			if (strncmp(readBuff, package.c_str(), package.length())) continue;
			const char endChar = readBuff[package.length()];
			if (endChar != ':' && endChar != 0)continue;

			appInfo.pids.emplace_back(pid);
		}
		closedir(dir);
		END_TIME_COUNT;
	}

        //临时解冻
    void unFreezerTemporary(set<int>& uids) {
        curForegroundApp.insert(uids.begin(), uids.end());
        updateAppProcess();
    }

    void unFreezerTemporary(int uid) {
        curForegroundApp.insert(uid);
        updateAppProcess();
    }

	map<int, vector<int>> getRunningPids(set<int>& uidSet) {
		START_TIME_COUNT;
		map<int, vector<int>> pids;

		DIR* dir = opendir("/proc");
		if (dir == nullptr) {
			char errTips[256];
			snprintf(errTips, sizeof(errTips), "错误: %s() [%d]:[%s]", __FUNCTION__, errno,
				strerror(errno));
			fprintf(stderr, "%s", errTips);
			frozen.log(errTips);
			return pids;
		}

		struct dirent* file;
		char fullPath[64];
		memcpy(fullPath, "/proc/", 6);
		while ((file = readdir(dir)) != nullptr) {
			if (file->d_name[0] < '0' || file->d_name[0] > '9') continue;

			const int pid = atoi(file->d_name);
			if (pid <= 100) continue;

			memcpy(fullPath + 6, file->d_name, 6);

			struct stat statBuf;
			const int uid = statBuf.st_uid;
			if (stat(fullPath, &statBuf) || !uidSet.contains(uid))continue;

			strcat(fullPath + 8, "/cmdline");
			char readBuff[256];
			if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
			const string& package = managedApp[uid].package;
			if (strncmp(readBuff, package.c_str(), package.length())) continue;

			pids[uid].emplace_back(pid);
		}
		closedir(dir);
		END_TIME_COUNT;
		return pids;
	}

	set<int> getRunningUids(set<int>& uidSet) {
		START_TIME_COUNT;
		set<int> uids;

		DIR* dir = opendir("/proc");
		if (dir == nullptr) {
			char errTips[256];
			snprintf(errTips, sizeof(errTips), "错误: %s() [%d]:[%s]", __FUNCTION__, errno,
				strerror(errno));
			fprintf(stderr, "%s", errTips);
			frozen.log(errTips);
			return uids;
		}

		char fullPath[64];
		memcpy(fullPath, "/proc/", 6);
		struct dirent* file;
		while ((file = readdir(dir)) != nullptr) {
			if (file->d_name[0] < '0' || file->d_name[0] > '9') continue;

			const int pid = atoi(file->d_name);
			if (pid <= 100) continue;

			memcpy(fullPath + 6, file->d_name, 6);

			struct stat statBuf;
			const int uid = statBuf.st_uid;
			if (stat(fullPath, &statBuf) || !uidSet.contains(uid))continue;

			strcat(fullPath + 8, "/cmdline");
			char readBuff[256];
			if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
			const string& package = managedApp[uid].package;
			if (strncmp(readBuff, package.c_str(), package.length())) continue;

			uids.insert(uid);
		}
		closedir(dir);
		END_TIME_COUNT;
		return uids;
	}

	void handleSignal(const appInfoStruct& appInfo, const int signal) {
		if (signal == SIGKILL) {
			if (isV1Mode() && appInfo.isFreezeMode())
				handleFreezer(appInfo, false);  // 先给V1解冻， 否则无法杀死

			//先暂停 然后再杀，否则有可能会复活
			usleep(1000 * 50);
			for (const auto pid : appInfo.pids)
				kill(pid, SIGSTOP);

			usleep(1000 * 50);
			for (const auto pid : appInfo.pids)
				kill(pid, SIGKILL);

			return;
		}

		for (const int pid : appInfo.pids)
			if (kill(pid, signal) < 0 && signal == SIGSTOP)
				frozen.logFmt("SIGSTOP冻结 [%s PID:%d] 失败[%s]",
					appInfo.label.c_str(), pid, strerror(errno));
	}

	void handleFreezer(const appInfoStruct& appInfo, const bool freeze) {
		char path[256];

		switch (workMode) {
		case WORK_MODE::V2FROZEN: {
			for (const int pid : appInfo.pids) {
                if (!Utils::writeInt(freeze ? cgroupV2FrozenPath : cgroupV2UnfrozenPath, pid))
                    frozen.logFmt("%s [%s PID:%d] 失败(V2FROZEN)",
                        freeze ? "冻结" : "解冻", appInfo.label.c_str(), pid);
			}
		} break;

		case WORK_MODE::V2UID: {
            for (const int pid : appInfo.pids) {
                snprintf(path, sizeof(path), cgroupV2UidPidPath, appInfo.uid, pid);
                if (!Utils::writeString(path, freeze ? "1" : "0", 2))
                    frozen.logFmt("%s [%s PID:%d] 失败(进程已死亡)",
                        freeze ? "冻结" : "解冻", appInfo.label.c_str(), pid);
            }
		} break;

		case WORK_MODE::V1F: {
			for (const int pid : appInfo.pids) {
				if (!Utils::writeInt(freeze ? cgroupV1FrozenPath : cgroupV1UnfrozenPath, pid))
					frozen.logFmt("%s [%s PID:%d] 失败(V1FROZEN)",
                        freeze ? "冻结" : "解冻", appInfo.label.c_str(), pid);
			}
		} break;

		// 本函数只处理Freezer模式，其他冻结模式不应来到此处
		default: {
			frozen.logFmt("%s 使用了错误的冻结模式", appInfo.label.c_str());
		} break;
		}
	}

	// 只接受 SIGSTOP SIGCONT
	int handleProcess(appInfoStruct& appInfo, const bool freeze) {
		START_TIME_COUNT;

        if (freeze) {
            getPids(appInfo);
        }
        else {
            erase_if(appInfo.pids, [](const int pid) {
                char path[16];
                snprintf(path, sizeof(path), "/proc/%d", pid);
                return access(path, F_OK);
                });
        }

		switch (appInfo.freezeMode) {
		case FREEZE_MODE::FREEZER: 
		case FREEZE_MODE::FREEZER_BREAK: {
			if (workMode != WORK_MODE::GLOBAL_SIGSTOP) {
				const int res = handleBinder(appInfo, freeze);
				if (res < 0 && freeze && appInfo.isPermissive) return res;
					
				handleFreezer(appInfo, freeze);
				break;
			}
			// 如果是全局 WORK_MODE::GLOBAL_SIGSTOP 则顺着执行下面
		}

		case FREEZE_MODE::SIGNAL:
		case FREEZE_MODE::SIGNAL_BREAK: {
			const int res = handleBinder(appInfo, freeze);
			if (res < 0 && freeze && appInfo.isPermissive) return res;

			handleSignal(appInfo, freeze ? SIGSTOP : SIGCONT);
		} break;

		case FREEZE_MODE::TERMINATE: {
            if (freeze)
                handleSignal(appInfo, SIGKILL);
			return 0;
		}

		default: {
			frozen.logFmt("不再冻结此应用：%s %s", appInfo.label.c_str(),
				getModeText(appInfo.freezeMode).c_str());
			return 0;
		}
		}

		if (settings.wakeupTimeoutMin != 120) {
			// 无论冻结还是解冻都要清除 解冻时间线上已设置的uid
			auto it = unfrozenIdx.find(appInfo.uid);
			if (it != unfrozenIdx.end())
				unfrozenTimeline[it->second] = 0;

			// 冻结就需要在 解冻时间线 插入下一次解冻的时间
			if (freeze && appInfo.pids.size() && appInfo.isSignalOrFreezer()) {
				uint32_t nextIdx = (timelineIdx + settings.wakeupTimeoutMin * 60) & 0x0FFF; // [ %4096]
				unfrozenIdx[appInfo.uid] = nextIdx;
				unfrozenTimeline[nextIdx] = appInfo.uid;
			}
			else {
				unfrozenIdx.erase(appInfo.uid);
			}
		}

		if (freeze && appInfo.needBreakNetwork()) 
            BreakNetwork(appInfo);
        else if (freeze && !appInfo.isPermissive && settings.enableBreakNetwork) 
            BreakNetwork(appInfo);

		END_TIME_COUNT;
		return appInfo.pids.size();
	}

	void BreakNetwork(const appInfoStruct& appInfo) {
        const auto& ret = systemTools.breakNetworkByLocalSocket(appInfo.uid);
        switch (static_cast<REPLY>(ret)) {
            case REPLY::SUCCESS:
                frozen.logFmt("断网成功: %s", appInfo.label.c_str());
                break;
            case REPLY::FAILURE:
                frozen.logFmt("断网失败: %s", appInfo.label.c_str());
                break;
            default:
                frozen.logFmt("断网 未知回应[%d] %s", ret, appInfo.label.c_str());
                break;
        }
    }

	void MemoryRecycle(const appInfoStruct& appInfo) {
        if (!settings.enableMemoryRecycle) return;
        char path[24];

        for (const int pid : appInfo.pids) {
            snprintf(path, sizeof(path), "/proc/%d/reclaim", pid);
            Utils::FileWrite(path, "file");
            if (settings.enableDebug)  frozen.logFmt("内存回收: %s PID:%d 类型:文件", appInfo.label.c_str(), pid);
        }
    }

	// 重新压制第三方。 白名单, 前台, 待冻结列队 都跳过
	void checkReFreeze() {
		START_TIME_COUNT;

		if (--refreezeSecRemain > 0) return;

		refreezeSecRemain = settings.getRefreezeTimeout();

		map<int, vector<int>> terminateList, SIGSTOPList, freezerList;

		DIR* dir = opendir("/proc");
		if (dir == nullptr) {
			char errTips[256];
			snprintf(errTips, sizeof(errTips), "错误: %s() [%d]:[%s]", __FUNCTION__, errno,
				strerror(errno));
			fprintf(stderr, "%s", errTips);
			frozen.log(errTips);
			return;
		}

		char fullPath[64];
		memcpy(fullPath, "/proc/", 6);
		struct dirent* file;
		
		while ((file = readdir(dir)) != nullptr) {
			if (file->d_name[0] < '0' || file->d_name[0] > '9') continue;

			int pid = atoi(file->d_name);
			if (pid <= 100) continue;

			memcpy(fullPath + 6, file->d_name, 6);

			struct stat statBuf;
			const int uid = statBuf.st_uid;
			if (stat(fullPath, &statBuf) || managedApp.without(uid))continue;

			auto& appInfo = managedApp[uid];
			if (appInfo.isWhitelist() || pendingHandleList.contains(uid) || curForegroundApp.contains(uid))
				continue;

			strcat(fullPath + 8, "/cmdline");
			char readBuff[256];
			if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
			if (strncmp(readBuff, appInfo.package.c_str(), appInfo.package.length())) continue;

			switch (appInfo.freezeMode) {
			case FREEZE_MODE::TERMINATE:
				terminateList[uid].emplace_back(pid);
				break;
			case FREEZE_MODE::FREEZER:
			case FREEZE_MODE::FREEZER_BREAK:
				if (workMode != WORK_MODE::GLOBAL_SIGSTOP) {
					freezerList[uid].emplace_back(pid);
					break;
				}
			case FREEZE_MODE::SIGNAL:
			case FREEZE_MODE::SIGNAL_BREAK:
			default:
				SIGSTOPList[uid].emplace_back(pid);
				break;
			}
		}
		closedir(dir);

		//vector<int> breakList;
		stackString<1024> tmp;
		for (const auto& [uid, pids] : freezerList) {
			auto& appInfo = managedApp[uid];
			tmp.append(" ", 1).append(appInfo.label.c_str(), (int)appInfo.label.length());
			handleFreezer(appInfo, true);
			managedApp[uid].pids = std::move(pids);

			//if (appInfo.needBreakNetwork())
			//	breakList.emplace_back(uid);
		}
		if (tmp.length) frozen.logFmt("定时Freezer压制: %s", tmp.c_str());

		tmp.clear();
		for (auto& [uid, pids] : SIGSTOPList) {
			auto& appInfo = managedApp[uid];
			tmp.append(" ", 1).append(appInfo.label.c_str(), (int)appInfo.label.length());
			handleSignal(appInfo, SIGSTOP);
			managedApp[uid].pids = std::move(pids);

			//if (appInfo.needBreakNetwork())
			//	breakList.emplace_back(uid);
		}
		if (tmp.length) frozen.logFmt("定时SIGSTOP压制: %s", tmp.c_str());

		tmp.clear();
		for (const auto& [uid, pids] : terminateList) {
			auto& appInfo = managedApp[uid];
			auto& label = managedApp[uid].label;
			tmp.append(" ", 1).append(label.c_str(), (int)label.length());
			handleSignal(appInfo, SIGKILL);
		}
		if (tmp.length) frozen.logFmt("定时压制 杀死后台: %s", tmp.c_str());

		//for (const int uid : breakList) {
		//	usleep(1000 * 10);
		//	systemTools.breakNetworkByLocalSocket(uid);
		//	frozen.logFmt("定时压制 断网 [%s]", managedApp[uid].label.c_str());
		//}

		END_TIME_COUNT;
	}

    bool mountFreezerV1() {
        if (!access("/dev/jark_freezer", F_OK)) // 已挂载
            return true;

        // https://man7.org/linux/man-pages/man7/cgroups.7.html
        // https://www.kernel.org/doc/Documentation/cgroup-v1/freezer-subsystem.txt
        // https://www.containerlabs.kubedaily.com/LXC/Linux%20Containers/The-cgroup-freezer-subsystem.html

        mkdir("/dev/Frozen", 0666);
        mount("freezer", "/dev/Frozen", "cgroup", 0, "freezer");
        usleep(1000 * 100);
        mkdir("/dev/Frozen/frozen", 0666);
        mkdir("/dev/Frozen/unfrozen", 0666);
        usleep(1000 * 100);
        Utils::writeString("/dev/Frozen/frozen/freezer.state", "FROZEN");
        Utils::writeString("/dev/Frozen/unfrozen/freezer.state", "THAWED");

        // https://www.spinics.net/lists/cgroups/msg24540.html
        // https://android.googlesource.com/device/google/crosshatch/+/9474191%5E%21/
        Utils::writeString("/dev/Frozen/frozen/freezer.killable", "1"); // 旧版内核不支持
        usleep(1000 * 100);

        return (!access(cgroupV1FrozenPath, F_OK) && !access(cgroupV1UnfrozenPath, F_OK));
    }
	
	bool checkFreezerV2UID() {
		return (!access(cgroupV2FreezerCheckPath, F_OK));
	}

	bool checkFreezerV2FROZEN() {
		return (!access(cgroupV2frozenCheckPath, F_OK) && !access(cgroupV2unfrozenCheckPath, F_OK));
	}

	void checkAndMountV2() {
		// https://cs.android.com/android/kernel/superproject/+/common-android12-5.10:common/kernel/cgroup/freezer.c

		if (checkFreezerV2UID())
			frozen.log("原生支持 FreezerV2(UID)");

		if (checkFreezerV2FROZEN()) {
			frozen.log("原生支持 FreezerV2(FROZEN)");
		}
		else {
			mkdir("/sys/fs/cgroup/frozen/", 0666);
			mkdir("/sys/fs/cgroup/unfrozen/", 0666);
			usleep(1000 * 500);

			if (checkFreezerV2FROZEN()) {
				auto fd = open(cgroupV2frozenCheckPath, O_WRONLY | O_TRUNC);
				if (fd > 0) {
					write(fd, "1", 2);
					close(fd);
				}
				frozen.logFmt("设置%s: FreezerV2(FROZEN)", fd > 0 ? "成功" : "失败");

				fd = open(cgroupV2unfrozenCheckPath, O_WRONLY | O_TRUNC);
				if (fd > 0) {
					write(fd, "0", 2);
					close(fd);
				}
				frozen.logFmt("设置%s: FreezerV2(UNFROZEN)", fd > 0 ? "成功" : "失败");

				frozen.log("现已支持 FreezerV2(FROZEN)");
			}
		}
	}

	void printProcState() {
		START_TIME_COUNT;

		DIR* dir = opendir("/proc");
		if (dir == nullptr) {
			frozen.logFmt("错误: %s(), [%d]:[%s]\n", __FUNCTION__, errno, strerror(errno));
			return;
		}

		int totalMiB = 0;
		bool needRefrezze = false;
		set<int> uidSet, pidSet;

		stackString<1024 * 16> stateStr("进程冻结状态:\n\n PID | MiB |  状 态  | 进 程\n");

		struct dirent* file;
		while ((file = readdir(dir)) != nullptr) {
			if (file->d_type != DT_DIR) continue;
			if (file->d_name[0] < '0' || file->d_name[0] > '9') continue;

			const int pid = atoi(file->d_name);
			if (pid <= 100) continue;

			char fullPath[64];
			memcpy(fullPath, "/proc/", 6);
			memcpy(fullPath + 6, file->d_name, 6);

			struct stat statBuf;
			if (stat(fullPath, &statBuf))continue;
			const int uid = statBuf.st_uid;
			if (managedApp.without(uid)) continue;

			auto& appInfo = managedApp[uid];
			if (appInfo.isWhitelist()) continue;

			strcat(fullPath + 8, "/cmdline");
			char readBuff[256]; // now is cmdline Content
			if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
			if (strncmp(readBuff, appInfo.package.c_str(), appInfo.package.length())) continue;

			uidSet.insert(uid);
			pidSet.insert(pid);

			stackString<256> label(appInfo.label.c_str(), appInfo.label.length());
			if (readBuff[appInfo.package.length()] == ':')
				label.append(readBuff + appInfo.package.length());

			memcpy(fullPath + 6, file->d_name, 6);
			strcat(fullPath + 8, "/statm");
			Utils::readString(fullPath, readBuff, sizeof(readBuff)); // now is statm content
			const char* ptr = strchr(readBuff, ' ');

			// Unit: 1 page(4KiB) convert to MiB. (atoi(ptr) * 4 / 1024)
			const int memMiB = ptr ? (atoi(ptr + 1) >> 8) : 0;
			totalMiB += memMiB;

			if (curForegroundApp.contains(uid)) {
				stateStr.appendFmt("%5d %4d 📱正在前台 %s\n", pid, memMiB, label.c_str());
				continue;
			}

			if (pendingHandleList.contains(uid)) {
				stateStr.appendFmt("%5d %4d ⏳等待冻结 %s\n", pid, memMiB, label.c_str());
				continue;
			}

			memcpy(fullPath + 6, file->d_name, 6);
			strcat(fullPath + 8, "/wchan");
			if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0) {
				uidSet.erase(uid);
				pidSet.erase(pid);
				continue;
			}

			stateStr.appendFmt("%5d %4d ", pid, memMiB);
			if (!strcmp(readBuff, v2wchan) || !strcmp(readBuff, v2xwchan)) {
				stateStr.appendFmt("❄️V2冻结中 %s\n", label.c_str());
			}
			else if (!strcmp(readBuff, v1wchan)) {
				stateStr.appendFmt("❄️V1冻结中 %s\n", label.c_str());
			}
			else if (!strcmp(readBuff, SIGSTOPwchan)) {
				stateStr.appendFmt("🧊ST冻结中 %s\n", label.c_str());
			}
			else if (!strcmp(readBuff, binder_wchan)) {
				stateStr.appendFmt("⚠️运行中(Binder通信) %s\n", label.c_str());
				needRefrezze = true;
			}
			else if (!strcmp(readBuff, pipe_wchan)) {
				stateStr.appendFmt("⚠️运行中(管道通信) %s\n", label.c_str());
				needRefrezze = true;
			}
			else if (!strcmp(readBuff, epoll_wait1_wchan) || !strcmp(readBuff, epoll_wait2_wchan)) {
				stateStr.appendFmt("⚠️运行中(就绪态) %s\n", label.c_str());
				needRefrezze = true;
			}
			else {
				stateStr.appendFmt("⚠️运行中(%s) %s\n", (const char*)readBuff, label.c_str());
				needRefrezze = true;
			}
		}
		closedir(dir);

		if (uidSet.size() == 0) {
			frozen.log("设为冻结的应用没有运行");
		}
		else {

			if (needRefrezze) {
				stateStr.append("\n ⚠️ 发现 [未冻结] 的进程, 即将进行冻结 ⚠️\n", 65);
				refreezeSecRemain = 0;
			}

			stateStr.appendFmt("\n总计 %d 应用 %d 进程, 占用内存 ", (int)uidSet.size(), (int)pidSet.size());
			stateStr.appendFmt("%.2f GiB", totalMiB / 1024.0);
			if (isV1Mode())
				stateStr.append(", V1已冻结状态可能会识别为[运行中]，请到[CPU使用时长]页面查看是否跳动", 98);

			frozen.log(stateStr.c_str());
		}
		END_TIME_COUNT;
	}

	// 解冻新APP, 旧APP加入待冻结列队 call once per 0.5 sec when Touching
	void updateAppProcess() {
		vector<int> newShowOnApp, toBackgroundApp;

		for (const int& uid : curForegroundApp)
			 if (lastForegroundApp.find(uid)  == lastForegroundApp.end())
				newShowOnApp.emplace_back(uid);

		for (const int& uid : lastForegroundApp)
			if (curForegroundApp.find(uid)  == curForegroundApp.end())
				toBackgroundApp.emplace_back(uid);

		if (newShowOnApp.empty() && toBackgroundApp.empty())
			return;

		lastForegroundApp = curForegroundApp;
			

		for (const int& uid : newShowOnApp) {
			// 如果在待冻结列表则只需移除
			if (pendingHandleList.erase(uid))
				continue;

			// 更新[打开时间]  并解冻
			auto& appInfo = managedApp[uid];
			appInfo.startRunningTime = time(nullptr);

			const int num = handleProcess(appInfo, false);
			if (num > 0) frozen.logFmt("☀️解冻 %s %d进程", appInfo.label.c_str(), num);
			else frozen.logFmt("😁打开 %s", appInfo.label.c_str());
		}

		for (const int& uid : toBackgroundApp) // 更新倒计时
			pendingHandleList[uid] = managedApp[uid].isTerminateMode() ? 
			settings.terminateTimeout : settings.freezeTimeout;
	}

	// 处理待冻结列队 call once per 1sec
	void processPendingApp() {
		auto it = pendingHandleList.begin();
		while (it != pendingHandleList.end()) {
			auto& remainSec = it->second;
			if (--remainSec > 0) {//每次轮询减一
				it++;
				continue;
			}

			const int uid = it->first;
			auto& appInfo = managedApp[uid];
			int num = handleProcess(appInfo, true);
			if (num < 0) {
				if (appInfo.delayCnt >= 5) {
                    handleSignal(appInfo, SIGKILL);
                    frozen.logFmt("%s:%d 已延迟%d次, 强制杀死", appInfo.label.c_str(), -num, appInfo.delayCnt);
                    num = 0;
                }
                else {
                    appInfo.delayCnt++;
                    remainSec = 15 << appInfo.delayCnt;
                    frozen.logFmt("%s:%d Binder正在传输, 第%d次延迟, %d%s 后再冻结", appInfo.label.c_str(), -num,
                        appInfo.delayCnt, remainSec < 60 ? remainSec : remainSec / 60, remainSec < 60 ? "秒" : "分");
                    it++;
                    continue;
                }
			}
			MemoryRecycle(appInfo);
			it = pendingHandleList.erase(it);
			appInfo.delayCnt = 0;

			appInfo.stopRunningTime = time(nullptr);
			const int delta = appInfo.startRunningTime == 0 ? 0:
				(appInfo.stopRunningTime - appInfo.startRunningTime);
			appInfo.totalRunningTime += delta;
			const int total = appInfo.totalRunningTime;

			stackString<128> timeStr("运行");
			if (delta >= 3600)
				timeStr.appendFmt("%d时", delta / 3600);
			if (delta >= 60)
				timeStr.appendFmt("%d分", (delta % 3600) / 60);
			timeStr.appendFmt("%d秒", delta % 60);

			timeStr.append(" 累计", 7);
			if (total >= 3600)
				timeStr.appendFmt("%d时", total / 3600);
			if (total >= 60)
				timeStr.appendFmt("%d分", (total % 3600) / 60);
			timeStr.appendFmt("%d秒", total % 60);

			if (num)
				frozen.logFmt("%s冻结 %s %d进程 %s",
					appInfo.isSignalMode() ? "🧊" : "❄️",
					appInfo.label.c_str(), num, timeStr.c_str());
			else frozen.logFmt("😭关闭 %s %s", appInfo.label.c_str(), *timeStr);
		}
	}

	void checkWakeup() {
		timelineIdx = (timelineIdx + 1) & 0x0FFF; // [ %4096]
		const auto uid = unfrozenTimeline[timelineIdx];
		if (uid == 0) return;

		unfrozenTimeline[timelineIdx] = 0;//清掉时间线当前位置UID信息

		if (managedApp.without(uid)) return;

		auto& appInfo = managedApp[uid];
		if (appInfo.isSignalOrFreezer()) {
			const int num = handleProcess(appInfo, false);
			if (num > 0) {
				appInfo.startRunningTime = time(nullptr);
				pendingHandleList[uid] = settings.freezeTimeout;//更新待冻结倒计时
				frozen.logFmt("☀️定时解冻 %s %d进程", appInfo.label.c_str(), num);
			}
			else {
				frozen.logFmt("🗑️后台被杀 %s", appInfo.label.c_str());
			}
		}
		else {
			unfrozenIdx.erase(uid);
		}
	}


	// 常规查询前台 只返回第三方, 剔除白名单/桌面
	void getVisibleAppByShell() {
		START_TIME_COUNT;

		curForegroundApp.clear();
		const char* cmdList[] = { "/system/bin/cmd", "cmd", "activity", "stack", "list", nullptr };
		VPOPEN::vpopen(cmdList[0], cmdList + 1, getVisibleAppBuff.get(), GET_VISIBLE_BUF_SIZE);

		stringstream ss;
		ss << getVisibleAppBuff.get();

		// 以下耗时仅为 VPOPEN::vpopen 的 2% ~ 6%
		string line;
		while (getline(ss, line)) {
			if (!managedApp.hasHomePackage() && line.find("mActivityType=home") != string::npos) {
				getline(ss, line); //下一行就是桌面信息
				auto startIdx = line.find_last_of('{');
				auto endIdx = line.find_last_of('/');
				if (startIdx == string::npos || endIdx == string::npos || startIdx > endIdx)
					continue;

				managedApp.updateHomePackage(line.substr(startIdx + 1, endIdx - (startIdx + 1)));
			}

			//  taskId=8655: com.ruanmei.ithome/com.ruanmei.ithome.ui.MainActivity bounds=[0,1641][1440,3200]
			//     userId=0 visible=true topActivity=ComponentappInfo{com.ruanmei.ithome/com.ruanmei.ithome.ui.NewsappInfoActivity}
			if (!line.starts_with("  taskId=")) continue;
			if (line.find("visible=true") == string::npos) continue;

			auto startIdx = line.find_last_of('{');
			auto endIdx = line.find_last_of('/');
			if (startIdx == string::npos || endIdx == string::npos || startIdx > endIdx) continue;

			const string& package = line.substr(startIdx + 1, endIdx - (startIdx + 1));
			if (managedApp.without(package)) continue;
			int uid = managedApp.getUid(package);
			if (managedApp[uid].isWhitelist()) continue;
			curForegroundApp.insert(uid);
		}

		if (curForegroundApp.size() >= (lastForegroundApp.size() + 3)) //有时系统会虚报大量前台应用
			curForegroundApp = lastForegroundApp;

		END_TIME_COUNT;
	}

	void getVisibleAppByLocalSocket() {
		START_TIME_COUNT;

		int buff[64];
		int recvLen = Utils::localSocketRequest(XPOSED_CMD::GET_FOREGROUND, nullptr, 0, buff,
			sizeof(buff));

		int& UidLen = buff[0];
		if (recvLen <= 0) {
			frozen.logFmt("%s() 工作异常, 请确认LSPosed中Frozen勾选系统框架, 然后重启", __FUNCTION__);
			END_TIME_COUNT;
			return;
		}
		else if (UidLen > 16 || (UidLen != (recvLen / 4 - 1))) {
			frozen.logFmt("%s() 前台服务数据异常 UidLen[%d] recvLen[%d]", __FUNCTION__, UidLen, recvLen);
			frozen.logFmt("DumpHex: %s", Utils::bin2Hex(buff, recvLen < 64 * 4 ? recvLen : 64 * 4).c_str());
			END_TIME_COUNT;
			return;
		}

		curForegroundApp.clear();
		for (int i = 1; i <= UidLen; i++) {
			int& uid = buff[i];
			if (managedApp.contains(uid)) curForegroundApp.insert(uid);
			else frozen.logFmt("非法UID[%d], 可能是新安装的应用, 请点击右上角第一个按钮更新应用列表", uid);
		}

#if DEBUG_DURATION
		string tmp;
		for (auto& uid : curForegroundApp)
			tmp += " [" + managedApp[uid].label + "]";
		if (tmp.length())
			frozen.logFmt("LOCALSOCKET前台%s", tmp.c_str());
		else
			frozen.log("LOCALSOCKET前台 空");
#endif
		END_TIME_COUNT;
	}


	string getModeText(FREEZE_MODE mode) {
		switch (mode) {
		case FREEZE_MODE::TERMINATE:
			return "杀死后台";
		case FREEZE_MODE::SIGNAL:
			return "SIGSTOP冻结";
		case FREEZE_MODE::SIGNAL_BREAK:
			return "SIGSTOP冻结断网";
		case FREEZE_MODE::FREEZER:
			return "Freezer冻结";
		case FREEZE_MODE::FREEZER_BREAK:
			return "Freezer冻结断网";
		case FREEZE_MODE::WHITELIST:
			return "自由后台";
		case FREEZE_MODE::WHITEFORCE:
			return "自由后台(内置)";
		default:
			return "未知";
		}
	}

    int getReKernelPort() {
        char buffer[256];
        DIR *dir = opendir("/proc/rekernel");
        struct dirent *file;

        while ((file = readdir(dir)) != nullptr) {
            if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) continue;
            strncpy(buffer, file->d_name, sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = 0;
            break;
        }

        closedir(dir);
        return atoi(buffer);
    }

    int ReKernelMagiskFunc() {
        if (!settings.enableReKernel) return 0;
        if (settings.enableBinderFreeze) {
            frozen.log("检测到你开启了全局冻结Binder,这会导致ReKernel工作异常,所以已结束与ReKernel的通信"); 
            return 0;
        } 

        int skfd;
        int ret;
        user_msg_info u_info;
        socklen_t len;
        struct nlmsghdr* nlh = nullptr;
        struct sockaddr_nl saddr, daddr;
        const char* umsg = "Hello! Re:Kernel!";

        if (access("/proc/rekernel/", F_OK)) {
            frozen.log("ReKernel未安装");
            return -1;
        }

        const int NETLINK_UNIT = getReKernelPort();

        frozen.logFmt("已找到ReKernel通信端口:%d",NETLINK_UNIT);

        skfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_UNIT);
        if (skfd == -1) {
            sleep(10);
            frozen.log("创建NetLink失败\n");
            return -1;
        }
    
        memset(&saddr, 0, sizeof(saddr));
        saddr.nl_family = AF_NETLINK;
        saddr.nl_pid = USER_PORT;
        saddr.nl_groups = 0;

        if (bind(skfd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0) {
            frozen.log("连接Bind失败\n");
            close(skfd);
            return -1;
        }

        memset(&daddr, 0, sizeof(daddr));
        daddr.nl_family = AF_NETLINK;
        daddr.nl_pid = 0;
        daddr.nl_groups = 0;

        nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PLOAD));

        memset(nlh, 0, sizeof(struct nlmsghdr));
        nlh->nlmsg_len = NLMSG_SPACE(MAX_PLOAD);
        nlh->nlmsg_flags = 0;
        nlh->nlmsg_type = 0;
        nlh->nlmsg_seq = 0;
        nlh->nlmsg_pid = saddr.nl_pid;

        memcpy(NLMSG_DATA(nlh), umsg, strlen(umsg)); 

        #if DEBUG_DURATION
            frozen.logFmt("Send msg to kernel:%s", umsg);
        #endif

        ret = sendto(skfd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *)&daddr, sizeof(struct sockaddr_nl));
        if (!ret) {
            frozen.log("向ReKernel发送消息失败!\n 请检查您的ReKernel版本是否为最新版本!\n Frozen并不支持ReKernel KPM版本!");
            return -1;
        }

        frozen.log("与ReKernel握手成功");
        while (true) {
            memset(&u_info, 0, sizeof(u_info));
            len = sizeof(struct sockaddr_nl);
            ret = recvfrom(skfd, &u_info, sizeof(user_msg_info), 0, (struct sockaddr *)&daddr, &len);
            if (!ret) {
                frozen.log("从ReKernel接收消息失败！\n");
                close(skfd);
                return -1;
            }

         //   const bool isNetworkType = (strstr(u_info.msg,  "type=Binder") != nullptr);
            auto ptr = strstr(u_info.msg, "target=");

            #if DEBUG_DURATION
                frozen.logFmt("ReKernel发送的通知:%s", u_info.msg);
            #endif

           
           // if (!isNetworkType) continue;
            if (ptr != nullptr) {
                const int uid = atoi(ptr + 7);
                auto& appInfo = managedApp[uid];
                if (managedApp.contains(uid) && appInfo.isPermissive && !curForegroundApp.contains(uid) && !pendingHandleList.contains(uid) && appInfo.isBlacklist()) {
                    frozen.logFmt("[%s] 接收到Re:Kernel的Binder信息(SYNC), 类别: transaction, 将进行临时解冻", managedApp[uid].label.c_str());     
                    unFreezerTemporary(uid);      
                }              
            }     
        }
        close(skfd);  
        free(nlh); 
        return 0;
    }

    int NkBinderMagiskFunc() {
        sleep(3);
        if (settings.enableReKernel || settings.enableBinderFreeze) { frozen.log("您已开启ReKernel或开启Binder全局冻结 已自动结束与NkBinder的通信"); return 0; }
        int skfd = socket(AF_LOCAL, SOCK_STREAM, 0);
        int len = 0;
        struct sockaddr_un addr;
        char buffer[128];
        if (skfd < 0) {
            printf("socket failed\n");
            return -1;
        }
    
        addr.sun_family  = AF_LOCAL;
        addr.sun_path[0]  = 0;  
        memcpy(addr.sun_path + 1, "nkbinder", strlen("nkbinder") + 1);
    
        len = 1 + strlen("nkbinder") + offsetof(struct sockaddr_un, sun_path);
    
        if (connect(skfd, (struct sockaddr*)&addr, len) < 0) {
            printf("connect failed\n");
            close(skfd);
            return -1;
        }

        frozen.log("与NkBinder握手成功");
        while (true) {
            recv(skfd, buffer, sizeof(buffer), 0);

            #if DEBUG_DURATION
                printf("NkBinder: %s\n", buffer);
            #endif

            auto ptr = strstr(buffer, "from_uid=");

            if (ptr != nullptr) {
                const int uid = atoi(ptr + 9);
                auto& appInfo = managedApp[uid];
                if (managedApp.contains(uid) && appInfo.isPermissive && !curForegroundApp.contains(uid) && !pendingHandleList.contains(uid) && appInfo.isBlacklist()) {
                    frozen.logFmt("[%s] 接收到NkBinder的Binder信息(SYNC), 类别: transaction, 将进行临时解冻", managedApp[uid].label.c_str());     
                    unFreezerTemporary(uid);      
                } 
            }
            usleep(40000); //等待NkBinder处理完EBPF事件
        }
        close(skfd);
        return 0;
    }

	void ThreadsUnfreezeFunc() {
		if (doze.isScreenOffStandby && doze.checkIfNeedToExit()) {
			curForegroundApp = std::move(curFgBackup); // recovery
			updateAppProcess();
			setWakeupLockByLocalSocket(WAKEUP_LOCK::DEFAULT);		
		}
		else {
			getVisibleAppByLocalSocket();
			updateAppProcess(); // ~40us
		}		
	}

	void cpuSetTriggerTask() {
		int inotifyFd = inotify_init();
		if (inotifyFd < 0) {
			fprintf(stderr, "同步事件: 0xB0 (1/3)失败: [%d]:[%s]", errno, strerror(errno));
			exit(-1);
		}

		int watch_d = inotify_add_watch(inotifyFd,
			frozen.SDK_INT_VER >= 33 ? cpusetEventPathA13
			: cpusetEventPathA12,
			IN_ALL_EVENTS);

		if (watch_d < 0) {
			fprintf(stderr, "同步事件: 0xB0 (2/3)失败: [%d]:[%s]", errno, strerror(errno));
			exit(-1);
		}

		frozen.log("监听顶层应用切换成功");

		char buf[8192];
		while (read(inotifyFd, buf, sizeof(buf)) > 0) {
			ThreadsUnfreezeFunc();
			Utils::sleep_ms(200);// 防抖
			ThreadsUnfreezeFunc();
		}

		inotify_rm_watch(inotifyFd, watch_d);
		close(inotifyFd);

		frozen.log("已退出监控同步事件: 0xB0");
	}

	[[noreturn]] void cycleThreadFunc() {
		sleep(1);
		getVisibleAppByShell(); // 获取桌面

		while (true) {
			sleep(1);

			systemTools.cycleCnt++;

			processPendingApp();//1秒一次

			// 2分钟一次 在亮屏状态检测是否已经息屏  息屏状态则检测是否再次强制进入深度Doze
			if (doze.checkIfNeedToEnter()) {
				curFgBackup = std::move(curForegroundApp); //backup
				updateAppProcess();
				setWakeupLockByLocalSocket(WAKEUP_LOCK::IGNORE);
			}

			if (doze.isScreenOffStandby)continue;// 息屏状态 不用执行 以下功能

			systemTools.checkBattery();// 1分钟一次 电池检测
			checkReFreeze();// 重新压制切后台的应用
			checkWakeup();// 检查是否有定时解冻
		}
	}

	void getBlackListUidRunning(set<int>& uids) {
		uids.clear();

		START_TIME_COUNT;

		DIR* dir = opendir("/proc");
		if (dir == nullptr) {
			char errTips[256];
			snprintf(errTips, sizeof(errTips), "错误: %s() [%d]:[%s]", __FUNCTION__, errno,
				strerror(errno));
			fprintf(stderr, "%s", errTips);
			frozen.log(errTips);
			return;
		}

		char fullPath[64];
		memcpy(fullPath, "/proc/", 6);

		struct dirent* file;
		while ((file = readdir(dir)) != nullptr) {
			if (file->d_name[0] < '0' || file->d_name[0] > '9') continue;

			int pid = atoi(file->d_name);
			if (pid <= 100) continue;

			memcpy(fullPath + 6, file->d_name, 6);

			struct stat statBuf;
			if (stat(fullPath, &statBuf))continue;
			const int uid = statBuf.st_uid;
			if (!managedApp.contains(uid) || managedApp[uid].isWhitelist())
				continue;

			strcat(fullPath + 8, "/cmdline");
			char readBuff[256];
			if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
			const auto& package = managedApp[uid].package;
			if (strncmp(readBuff, package.c_str(), package.length())) continue;

			uids.insert(uid);
		}
		closedir(dir);
		END_TIME_COUNT;
	}

	int setWakeupLockByLocalSocket(const WAKEUP_LOCK mode) {
		static set<int> blackListUidRunning;
		START_TIME_COUNT;

		if (mode == WAKEUP_LOCK::IGNORE)
			getBlackListUidRunning(blackListUidRunning);

		if (blackListUidRunning.empty())return 0;

		int buff[64] = { static_cast<int>(blackListUidRunning.size()), static_cast<int>(mode) };
		int i = 2;
		for (const int uid : blackListUidRunning)
			buff[i++] = uid;

		const int recvLen = Utils::localSocketRequest(XPOSED_CMD::SET_WAKEUP_LOCK, buff,
			i * sizeof(int), buff, sizeof(buff));

		if (recvLen == 0) {
			frozen.logFmt("%s() 工作异常, 请确认LSPosed中Frozen勾选系统框架, 然后重启", __FUNCTION__);
			END_TIME_COUNT;
			return 0;
		}
		else if (recvLen != 4) {
			frozen.logFmt("%s() 返回数据异常 recvLen[%d]", __FUNCTION__, recvLen);
			if (recvLen > 0 && recvLen < 64 * 4)
				frozen.logFmt("DumpHex: %s", Utils::bin2Hex(buff, recvLen).c_str());
			END_TIME_COUNT;
			return 0;
		}
		END_TIME_COUNT;
		return buff[0];
	}

 	// https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/services/core/java/com/android/server/am/CachedAppOptimizer.java;l=753
    // https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/services/core/jni/com_android_server_am_CachedAppOptimizer.cpp;l=475
    // https://cs.android.com/android/platform/superproject/main/+/main:frameworks/native/libs/binder/IPCThreadState.cpp;l=1564
    // https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/drivers/android/binder.c;l=5615
    // https://elixir.bootlin.com/linux/latest/source/drivers/android/binder.c#L5412

    // return 0成功  小于0为操作失败的pid
    int handleBinder(appInfoStruct& appInfo, const bool freeze) {
        if (bs.fd <= 0 || settings.enableBinderFreeze) return 0;

        // https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/drivers/android/binder.c;l=5434
        // 100ms 等待传输事务完成
        binder_freeze_info binderInfo{ .pid = 0u, .enable = freeze ? 1u : 0u, .timeout_ms = 0u };
        binder_frozen_status_info statusInfo = { 0, 0, 0 };

        if (freeze) { // 冻结
            for (size_t i = 0; i < appInfo.pids.size(); i++) {
              //if (appInfo.package == "com.ss.android.ugc.aweme.mobile" || appInfo.package == "com.tencent.mobileqq" || appInfo.package == "com.tencent.mm") break; // 抖音冻结断网 重载 QQ 微信冻结断网
                binderInfo.pid = appInfo.pids[i];
                if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                    int errorCode = errno;

                    // ret == EAGAIN indicates that transactions have not drained.
                    // Call again to poll for completion.
                    switch (errorCode) {
                    case EAGAIN: // 11
                        break;
                    case EINVAL:  // 22  酷安经常有某进程无法冻结binder
                        break;
                    default:
                        frozen.logFmt("冻结 Binder 发生异常 [%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
                        break;
                    }
                    // 解冻已经被冻结binder的进程
                    binderInfo.enable = 0;
                    for (size_t j = 0; j < i; j++) {
                        binderInfo.pid = appInfo.pids[j];

                        //TODO 如果解冻失败？
                        if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                            errorCode = errno;
                            frozen.logFmt("撤消冻结：解冻恢复Binder发生错误：[%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
                        }
                    }
                    return -appInfo.pids[i];
                }
            }

            usleep(1000 * 200);

            for (size_t i = 0; i < appInfo.pids.size(); i++) {
                statusInfo.pid = appInfo.pids[i];
                if (ioctl(bs.fd, BINDER_GET_FROZEN_INFO, &statusInfo) < 0) {
                    int errorCode = errno;
                    frozen.logFmt("获取 [%s:%d] Binder 状态错误 ErrroCode:%d", appInfo.label.c_str(), statusInfo.pid, errorCode);
                }
                else if (statusInfo.sync_recv & 2) { // 冻结后发现仍有传输事务
                   if (settings.enableDebug) frozen.logFmt("%s 仍有Binder传输事务", appInfo.label.c_str());

                    // 解冻已经被冻结binder的进程
                    binderInfo.enable = 0;
                    for (size_t j = 0;  j < appInfo.pids.size(); j++) {
                        binderInfo.pid = appInfo.pids[j];

                        //TODO 如果解冻失败？
                        if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                            int errorCode = errno;
                            frozen.logFmt("撤消冻结：解冻恢复Binder发生错误：[%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
                        }
                    }
                    return -appInfo.pids[i];
                }
            }
        }
        else { // 解冻
            set<int> hasSync;
            for (size_t i = 0; i < appInfo.pids.size(); i++) {
                statusInfo.pid = appInfo.pids[i];
                if (ioctl(bs.fd, BINDER_GET_FROZEN_INFO, &statusInfo) < 0) {
                    int errorCode = errno;
                    frozen.logFmt("获取 [%s:%d] Binder 状态错误 ErrroCode:%d", appInfo.label.c_str(), statusInfo.pid, errorCode);
                }
                else {
                    // 注意各个二进制位差别
                    // https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/services/core/jni/com_android_server_am_CachedAppOptimizer.cpp;l=489
                    // https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/drivers/android/binder.c;l=5467
                    if (statusInfo.sync_recv & 1) {
                        frozen.logFmt("%s 冻结期间存在 同步传输 Sync transactions, 正在尝试解冻Binder", appInfo.label.c_str());
                        //TODO 要杀掉进程 PS:使用最优雅的方案 先解冻再查看是否杀死 而不是直接杀死
                        for (size_t j = 0; j < appInfo.pids.size(); j++) {
                            binderInfo.pid = appInfo.pids[j];
                            if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                                int errorCode = errno;
                                frozen.logFmt("解冻 Binder 发生异常 [%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);

                                char tmp[32];
                                snprintf(tmp, sizeof(tmp), "/proc/%d/cmdline", binderInfo.pid);
                                    
                                frozen.logFmt("cmdline:[%s]", Utils::readString(tmp).c_str());

                                if (access(tmp, F_OK)) {
                                    frozen.logFmt("进程已不在 [%s] %u", appInfo.label.c_str(), binderInfo.pid);
                                }
                                //TODO 再解冻一次，若失败，考虑杀死？
                                else if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                                    errorCode = errno;
                                    frozen.logFmt("重试解冻 Binder 发生异常 [%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
                                    hasSync.insert(statusInfo.pid);
                                }
                            }
                        }
                        usleep(1000 * 300); // 解冻三秒如果依旧在传输 Sync transactions 考虑杀死
                        if (statusInfo.sync_recv & 1) {
                            frozen.logFmt("%s Binder 事件依旧异常活跃, 即将杀死进程", appInfo.label.c_str());
                        }
                    }
                    
                    if (statusInfo.async_recv & 1 && settings.enableDebug) {
                        frozen.logFmt("%s 冻结期间存在 异步传输（不重要）", appInfo.label.c_str());
                    }
                    if (statusInfo.sync_recv & 2 && settings.enableDebug) {
                        frozen.logFmt("%s 冻结期间存在 未完成传输（不重要）TXNS_PENDING", appInfo.label.c_str());
                    }
                }
            }

            if (hasSync.size()) {
                for (auto it = appInfo.pids.begin(); it != appInfo.pids.end();) {
                    if (hasSync.contains(*it)) {
                        frozen.logFmt("杀掉进程 pid: %d", *it);
                        kill(*it, SIGKILL);
                        it = appInfo.pids.erase(it);
                    }
                    else {
                        it++;
                    }
                }
            }

            for (size_t i = 0; i < appInfo.pids.size(); i++) {
                binderInfo.pid = appInfo.pids[i];
                if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                    int errorCode = errno;
                    frozen.logFmt("解冻 Binder 发生异常 [%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);

                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "/proc/%d/cmdline", binderInfo.pid);
                        
                    frozen.logFmt("cmdline:[%s]", Utils::readString(tmp).c_str());

                    if (access(tmp, F_OK)) {
                        frozen.logFmt("进程已不在 [%s] %u", appInfo.label.c_str(), binderInfo.pid);
                    }
                    //TODO 再解冻一次，若失败，考虑杀死？
                    else if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                        errorCode = errno;
                        frozen.logFmt("重试解冻 Binder 发生异常 [%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
                        hasSync.insert(statusInfo.pid);
                    }
                }
            }
        }

        return 0;
    }

    void binder_close() {
        munmap(bs.mapped, bs.mapSize);
        close(bs.fd);
        bs.fd = -1;
    }

    void binderInit(const char* driver) {
        if (frozen.kernelVersion.main < 5 && frozen.kernelVersion.sub < 10) { // 小于5.10的内核不支持BINDER_FREEZE特性
            frozen.logFmt("内核版本低(%d.%d.%d)，不支持 BINDER_FREEZER 特性", 
                frozen.kernelVersion.main, frozen.kernelVersion.sub, frozen.kernelVersion.patch);
            return;
        }

        bs.fd = open(driver, O_RDWR | O_CLOEXEC);
        if (bs.fd < 0) {
            frozen.logFmt("Binder初始化失败 路径打开失败：[%s] [%d:%s]", driver, errno, strerror(errno));
            return;
        }

        struct binder_version b_ver { -1 };
        if ((ioctl(bs.fd, BINDER_VERSION, &b_ver) < 0) ||
            (b_ver.protocol_version != BINDER_CURRENT_PROTOCOL_VERSION)) {
            frozen.logFmt("Binder初始化失败 binder版本要求: %d  本机版本: %d", BINDER_CURRENT_PROTOCOL_VERSION,
                b_ver.protocol_version);
            close(bs.fd);
            bs.fd = -1;
            return;
        }
        else {
            frozen.logFmt("初始驱动 BINDER协议版本 %d", b_ver.protocol_version);
        }

        // https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/services/core/jni/com_android_server_am_CachedAppOptimizer.cpp;l=489
        binder_frozen_status_info info = { (uint32_t)getpid(), 0, 0 };
        if (ioctl(bs.fd, BINDER_GET_FROZEN_INFO, &info) < 0) {
            int ret = -errno;
            frozen.logFmt("Binder初始化失败 不支持 BINDER_FREEZER 特性 ErrroCode:%d", ret);
            close(bs.fd);
            bs.fd = -1;
            return;
        }
        else {
            frozen.log("特性支持 BINDER_FREEZER");
        }

        bs.mapped = mmap(NULL, bs.mapSize, PROT_READ, MAP_PRIVATE, bs.fd, 0);
        if (bs.mapped == MAP_FAILED) {
            frozen.logFmt("Binder初始化失败 Binder mmap失败 [%s] [%d:%s]", driver, errno, strerror(errno));
            close(bs.fd);
            bs.fd = -1;
            return;
        }
    }
    
};
