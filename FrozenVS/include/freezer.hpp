#pragma once

#include "utils.hpp"
#include "vpopen.hpp"
#include "managedApp.hpp"
#include "doze.hpp"
#include "freezeit.hpp"
#include "systemTools.hpp"


#define PACKET_SIZE      256
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
    Freezeit& freezeit;
    ManagedApp& managedApp;
    SystemTools& systemTools;
    Settings& settings;
    Doze& doze;

    vector<thread> threads;

    WORK_MODE workMode = WORK_MODE::GLOBAL_SIGSTOP;
    unordered_map<int, int> pendingHandleList;     //挂起列队 无论黑白名单 { uid, timeRemain:sec }
    unordered_set<int> lastForegroundApp;          //前台应用
    unordered_set<int> curForegroundApp;           //新前台应用
    unordered_set<int> curFgBackup;                //新前台应用备份 用于进入doze前备份， 退出后恢复
    unordered_set<int> naughtyApp;                 //冻结期间存在异常解冻或唤醒进程的应用
    vector<int> lastAudioApp;                      //上次播放音频的应用  
    vector<int> currentAudioApp;                   //正在播放音频的应用  
    mutex naughtyMutex;

    uint32_t timelineIdx = 0;
    uint32_t unfrozenTimeline[4096] = {};

    bool specialPlanV2Uid = false;

    static constexpr size_t GET_VISIBLE_BUF_SIZE = 256 * 1024;
    unique_ptr<char[]> getVisibleAppBuff;

    binder_state bs{ -1, nullptr, 128 * 1024ULL };

    static constexpr const char* cgroupV2FreezerOppoCheckPath = "/sys/fs/cgroup/system/uid_0/cgroup.freeze"; 
    static constexpr const char* cgroupV2FreezerCheckPath = "/sys/fs/cgroup/uid_0/cgroup.freeze";
    static constexpr const char* cgroupV2frozenCheckPath = "/sys/fs/cgroup/frozen/cgroup.freeze";       // "1" frozen
    static constexpr const char* cgroupV2unfrozenCheckPath = "/sys/fs/cgroup/unfrozen/cgroup.freeze";   // "0" unfrozen
    
    static constexpr const char* cpusetEventPath = "/dev/cpuset/top-app";
    //const char* cpusetEventPathA12 = "/dev/cpuset/top-app/tasks";
    //const char* cpusetEventPathA13 = "/dev/cpuset/top-app/cgroup.procs";

    static constexpr const char* cgroupV1FrozenPath = "/dev/jark_freezer/frozen/cgroup.procs";
    static constexpr const char* cgroupV1UnfrozenPath = "/dev/jark_freezer/unfrozen/cgroup.procs";

    // 如果直接使用 uid_xxx/cgroup.freeze 可能导致无法解冻
    static constexpr const char* cgroupV2UidPidOppoPath = "/sys/fs/cgroup/%s/uid_%d/pid_%d/cgroup.freeze";
    static constexpr const char* cgroupV2UidPidPath = "/sys/fs/cgroup/uid_%d/pid_%d/cgroup.freeze"; // "1"frozen   "0"unfrozen
    static constexpr const char* cgroupV2FrozenPath = "/sys/fs/cgroup/frozen/cgroup.procs";         // write pid
    static constexpr const char* cgroupV2UnfrozenPath = "/sys/fs/cgroup/unfrozen/cgroup.procs";     // write pid

    static constexpr const char* binderPath = "/dev/binder";

    static constexpr char v2wchan[] = "do_freezer_trap";      // FreezerV2冻结状态
    static constexpr char v1wchan[] = "__refrigerator";       // FreezerV1冻结状态
    static constexpr char SIGSTOPwchan[] = "do_signal_stop";  // SIGSTOP冻结状态
    static constexpr char v2xwchan[] = "get_signal";          // FreezerV2冻结状态 内联状态
    static constexpr char pStopwchan[] = "ptrace_stop";       // ptrace冻结状态
    static constexpr char epoll_wait1_wchan[] = "SyS_epoll_wait";
    static constexpr char epoll_wait2_wchan[] = "do_epoll_wait";
    static constexpr char binder_wchan[] = "binder_ioctl_write_read";
    static constexpr char pipe_wchan[] = "pipe_wait";

public:
    Freezer& operator=(Freezer&&) = delete;

    Freezer(Freezeit& freezeit, Settings& settings, ManagedApp& managedApp,
        SystemTools& systemTools, Doze& doze) :
        freezeit(freezeit), managedApp(managedApp), systemTools(systemTools),
        settings(settings), doze(doze) {

        getVisibleAppBuff = make_unique<char[]>(GET_VISIBLE_BUF_SIZE);

        binderInit(binderPath);

        threads.emplace_back(thread(&Freezer::cpuSetTriggerTask, this));      // 监控前台
        threads.emplace_back(thread(&Freezer::bootFreeze, this));             // 开机冻结
        threads.emplace_back(thread(&Freezer::binderEventTriggerTask, this)); // binder事件
        threads.emplace_back(thread(&Freezer::getAudioByLocalSocket, this));  // 监听音频播放 
        threads.emplace_back(thread(&Freezer::cycleThreadFunc, this));

        checkAndMountV2();
        switch (static_cast<WORK_MODE>(settings.setMode)) {
        case WORK_MODE::V2FROZEN: {
            if (checkFreezerV2FROZEN()) {
                workMode = WORK_MODE::V2FROZEN;
                freezeit.log("Freezer类型已设为 V2(FROZEN)");
                return;
            }
            freezeit.log("不支持自定义Freezer类型 V2(FROZEN)");
        } break;

        case WORK_MODE::V2UID: {
            if (checkFreezerV2UID()) {
                workMode = WORK_MODE::V2UID;
                freezeit.log("Freezer类型已设为 V2(UID)");
                return;
            }
            freezeit.log("不支持自定义Freezer类型 V2(UID)");
        } break;

        case WORK_MODE::V1FROZEN: {
            if (mountFreezerV1()) {
                workMode = WORK_MODE::V1FROZEN;
                freezeit.log("Freezer类型已设为 V1(FROZEN)");
                return;
            }
            freezeit.log("不支持自定义Freezer类型 V1(FROZEN)");
        } break;

        case WORK_MODE::GLOBAL_SIGSTOP: {
            workMode = WORK_MODE::GLOBAL_SIGSTOP;
            freezeit.log("已设置[全局SIGSTOP], [Freezer冻结]将变为[SIGSTOP冻结]");
        } return;
        }

        // 以上手动选择若不支持或失败，下面将进行自动选择
        if (checkFreezerV2FROZEN()) {
            workMode = WORK_MODE::V2FROZEN;
            freezeit.log("Freezer类型已设为 V2(FROZEN)");
        }
        else if (checkFreezerV2UID()) {
            workMode = WORK_MODE::V2UID;
            freezeit.log("Freezer类型已设为 V2(UID)");
        }
        else {
            workMode = WORK_MODE::GLOBAL_SIGSTOP;
            freezeit.log("已开启 [全局SIGSTOP] 冻结模式");
        }
    }

    const char* getCurWorkModeStr() {
        switch (workMode)
        {
            case WORK_MODE::V2FROZEN:       return "FreezerV2 (FROZEN)";
            case WORK_MODE::V2UID:          return "FreezerV2 (UID)";
            case WORK_MODE::V1FROZEN:       return "FreezerV1 (FROZEN)";
            case WORK_MODE::GLOBAL_SIGSTOP: return "全局SIGSTOP";
        }
        return "未知";
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
            freezeit.log(errTips);
            return;
        }

        struct dirent* file;
        while ((file = readdir(dir)) != nullptr) {
            if (file->d_type != DT_DIR || file->d_name[0] < '0' || file->d_name[0] > '9') continue;

            const int pid = Fastatoi(file->d_name);
            if (pid <= 100) continue;
            
            const size_t len = Faststrlen(file->d_name);
            char fullPath[64] = "/proc/";
            memcpy(fullPath + 6, file->d_name, len);

            struct stat statBuf;
            if (stat(fullPath, &statBuf) || statBuf.st_uid != (uid_t)appInfo.uid)continue;

            memcpy(fullPath + len + 6, "/cmdline", 9);

            char readBuff[256];
            if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
            const string& package = appInfo.package;
            if (strncmp(readBuff, package.c_str(), package.length())) continue;
            const char endChar = readBuff[package.length()]; // 特例 com.android.chrome_zygote 无法binder冻结
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
            freezeit.log(errTips);
            return pids;
        }

        struct dirent* file;
        while ((file = readdir(dir)) != nullptr) {
            if (file->d_type != DT_DIR || file->d_name[0] < '0' || file->d_name[0] > '9') continue;

            const int pid = Fastatoi(file->d_name);
            if (pid <= 100) continue;

            const size_t len = Faststrlen(file->d_name);
            char fullPath[64] = "/proc/";
            memcpy(fullPath + 6, file->d_name, len);

            struct stat statBuf;
            if (stat(fullPath, &statBuf))continue;
            const int uid = statBuf.st_uid;
            if (!uidSet.contains(uid))continue;

            memcpy(fullPath + len + 6, "/cmdline", 9);
            char readBuff[256];
            if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
            const string& package = managedApp[uid].package;
            if (strncmp(readBuff, package.c_str(), package.length())) continue;
            const char endChar = readBuff[package.length()]; // 特例 com.android.chrome_zygote 无法binder冻结
            if (endChar != ':' && endChar != 0)continue;

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
            freezeit.log(errTips);
            return uids;
        }

        struct dirent* file;
        while ((file = readdir(dir)) != nullptr) {
            if (file->d_type != DT_DIR || file->d_name[0] < '0' || file->d_name[0] > '9') continue;

            const int pid = Fastatoi(file->d_name);
            if (pid <= 100) continue;

            const size_t len = Faststrlen(file->d_name);
            char fullPath[64] = "/proc/";
            memcpy(fullPath + 6, file->d_name, len);

            struct stat statBuf;
            if (stat(fullPath, &statBuf))continue;
            const int uid = statBuf.st_uid;
            if (!uidSet.contains(uid))continue;

            memcpy(fullPath + len + 6, "/cmdline", 9);
            char readBuff[256];
            if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
            const string& package = managedApp[uid].package;
            if (strncmp(readBuff, package.c_str(), package.length())) continue;
            const char endChar = readBuff[package.length()]; // 特例 com.android.chrome_zygote 无法binder冻结
            if (endChar != ':' && endChar != 0)continue;

            uids.insert(uid);
        }
        closedir(dir);
        END_TIME_COUNT;
        return uids;
    }

    void handleSignal(const appInfoStruct& appInfo, const int signal) {
        if (signal == SIGKILL) {
            //先暂停 然后再杀，否则有可能会复活
            for (const auto pid : appInfo.pids) {
                freezeit.debugFmt("暂停 [%s:%d]", appInfo.label.c_str(), pid);
                kill(pid, SIGSTOP);
            }

            usleep(1000 * 50);
            for (const auto pid : appInfo.pids) {
                freezeit.debugFmt("终结 [%s:%d]", appInfo.label.c_str(), pid);
                kill(pid, SIGKILL);
            }

            return;
        }

        for (const int pid : appInfo.pids)
            if (kill(pid, signal) < 0 && signal == SIGSTOP)
                freezeit.logFmt("SIGSTOP冻结 [%s:%d] 失败[%s]",
                    appInfo.label.c_str(), pid, strerror(errno));
    }

    void handleFreezer(const appInfoStruct& appInfo, const bool freeze) {
        char path[256];

        switch (workMode) {
        case WORK_MODE::V2FROZEN: {
            for (const int pid : appInfo.pids) {
                if (!Utils::writeInt(freeze ? cgroupV2FrozenPath : cgroupV2UnfrozenPath, pid))
                    freezeit.logFmt("%s [%s PID:%d] 失败(V2FROZEN)",
                        freeze ? "冻结" : "解冻", appInfo.label.c_str(), pid);
            }
        } break;

        case WORK_MODE::V2UID: {
            for (const int pid : appInfo.pids) {
                if (specialPlanV2Uid) 
                    snprintf(path, sizeof(path), cgroupV2UidPidOppoPath, appInfo.isSystemApp ? "system" : "apps", appInfo.uid, pid);
                else 
                    snprintf(path, sizeof(path), cgroupV2UidPidPath, appInfo.uid, pid);

                if (!Utils::writeString(path, freeze ? "1" : "0", 2))
                    freezeit.logFmt("%s [%s PID:%d] 失败(进程可能已结束或者Freezer控制器尚未初始化PID路径)",
                        freeze ? "冻结" : "解冻", appInfo.label.c_str(), pid);
            }
        } break;

        case WORK_MODE::V1FROZEN: {
            for (const int pid : appInfo.pids) {
                if (!Utils::writeInt(freeze ? cgroupV1FrozenPath : cgroupV1UnfrozenPath, pid))
                    freezeit.logFmt("%s [%s PID:%d] 失败(V1FROZEN)",
                        freeze ? "冻结" : "解冻", appInfo.label.c_str(), pid);
            }
        } break;

        // 本函数只处理Freezer模式，其他冻结模式不应来到此处
        default: {
            freezeit.logFmt("%s 使用了错误的冻结模式", appInfo.label.c_str());
        } break;
        }
    }

    // < 0 : 冻结binder失败的pid， > 0 : 冻结成功的进程数
    int handleProcess(appInfoStruct& appInfo, const bool freeze) {
        START_TIME_COUNT;

        if (freeze) {
            getPids(appInfo);
        }
        else {
            erase_if(appInfo.pids, [&appInfo](const int pid) {
                char path[32] = {};
                
                //snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
                //return !Utils::readString(path).starts_with(appInfo.package);

                snprintf(path, sizeof(path), "/proc/%d", pid);
                struct stat statBuf {};
                if (stat(path, &statBuf)) return true;
                return (uid_t)appInfo.uid != statBuf.st_uid;
                });
        }

        switch (appInfo.freezeMode) {
        case FREEZE_MODE::FREEZER: 
        case FREEZE_MODE::FREEZER_BREAK: {
            if (workMode != WORK_MODE::GLOBAL_SIGSTOP) {
                if (settings.enableBinderFreezer) { 
                    const int res = handleBinder(appInfo, freeze);
                    if (res < 0 && freeze && appInfo.isPermissive)
                        return res;
                }
                handleFreezer(appInfo, freeze);
                break;
            }
            // 如果是全局 WORK_MODE::GLOBAL_SIGSTOP 则顺着执行下面
        }

        case FREEZE_MODE::SIGNAL:
        case FREEZE_MODE::SIGNAL_BREAK: {
            if (settings.enableBinderFreezer) { 
                const int res = handleBinder(appInfo, freeze);
                if (res < 0 && freeze && appInfo.isPermissive)
                    return res;
            }
            handleSignal(appInfo, freeze ? SIGSTOP : SIGCONT);
        } break;

        case FREEZE_MODE::TERMINATE: {
            if (freeze)
                handleSignal(appInfo, SIGKILL);
            return 0;
        }

        default: { // 刚刚切到白名单，但仍在 pendingHandleList 时，就会执行到这里
            //freezeit.logFmt("不再冻结此应用：%s %s", appInfo.label.c_str(),
            //    getModeText(appInfo.freezeMode).c_str());
            return 0;
        }
        }

        if (settings.isWakeupEnable()) {
            // 无论冻结还是解冻都要清除 解冻时间线上已设置的uid
            if(0 <= appInfo.timelineUnfrozenIdx && appInfo.timelineUnfrozenIdx < 4096)
                unfrozenTimeline[appInfo.timelineUnfrozenIdx] = 0;

            // 冻结就需要在 解冻时间线 插入下一次解冻的时间
            if (freeze && appInfo.pids.size() && appInfo.isSignalOrFreezer()) {
                int nextIdx = (timelineIdx + settings.getWakeupTimeout()) & 0x0FFF; // [ %4096]
                while (unfrozenTimeline[nextIdx])
                    nextIdx = (nextIdx + 1) & 0x0FFF;
                appInfo.timelineUnfrozenIdx = nextIdx;
                unfrozenTimeline[nextIdx] = appInfo.uid;
            }
            else {
                appInfo.timelineUnfrozenIdx = -1;
            }
        }
        
        appInfo.isFreeze = freeze; 

        if (freeze && (appInfo.needBreakNetwork() || settings.enableBreakNetWork)) 
            breakNetWork(appInfo);

        END_TIME_COUNT;
        return appInfo.pids.size();
    }

    void breakNetWork(appInfoStruct& appInfo) {
        const auto ret = systemTools.breakNetworkByLocalSocket(appInfo.uid);
        switch (static_cast<REPLY>(ret)) {
        case REPLY::SUCCESS:
            freezeit.logFmt("断网成功: %s", appInfo.label.c_str());
            break;
        case REPLY::FAILURE:
            freezeit.logFmt("断网失败: %s", appInfo.label.c_str());
            break;
        default:
            freezeit.logFmt("断网 未知回应[%d] %s", ret, appInfo.label.c_str());
            break;
        }
    }

    // 开机冻结
    void bootFreeze() {
        START_TIME_COUNT;
        if (!settings.enableBootFreeze) return;

        sleep(10);

        lock_guard<mutex> lock(naughtyMutex);

        if (naughtyApp.size() == 0) {
            DIR* dir = opendir("/proc");
            if (dir == nullptr) {
                char errTips[256];
                snprintf(errTips, sizeof(errTips), "错误: %s() [%d]:[%s]", __FUNCTION__, errno,
                    strerror(errno));
                fprintf(stderr, "%s", errTips);
                freezeit.log(errTips);
                return;
            }

            struct dirent* file;
            while ((file = readdir(dir)) != nullptr) {
                if (file->d_type != DT_DIR || file->d_name[0] < '0' || file->d_name[0] > '9') continue;

                const int pid = Fastatoi(file->d_name);
                if (pid <= 100) continue;

                const size_t len = Faststrlen(file->d_name);
                char fullPath[64] = "/proc/";
                memcpy(fullPath + 6, file->d_name, len);

                struct stat statBuf;
                if (stat(fullPath, &statBuf))continue;
                const int uid = statBuf.st_uid;
                if (!managedApp.contains(uid) || pendingHandleList.contains(uid) || curForegroundApp.contains(uid))
                    continue;

                auto& appInfo = managedApp[uid];
                if (appInfo.isWhitelist())
                    continue;

                memcpy(fullPath + len + 6, "/cmdline", 9);
                char readBuff[256];
                if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
                const auto& package = appInfo.package;
                if (strncmp(readBuff, package.c_str(), package.length())) continue;
                const char endChar = readBuff[package.length()]; // 特例 com.android.chrome_zygote 无法binder冻结
                if (endChar != ':' && endChar != 0)continue;

                memcpy(fullPath + 6, file->d_name, len);
                memcpy(fullPath + len + 6, "/wchan", 7);
                if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
                if (strcmp(readBuff, v2wchan) && strcmp(readBuff, v1wchan) && strcmp(readBuff, SIGSTOPwchan) && 
                    strcmp(readBuff, v2xwchan) && strcmp(readBuff, pStopwchan)) {
                    naughtyApp.insert(uid);
                }
            }
            closedir(dir);
        }

        stackString<1024> tmp("开机冻结");
        for (const auto uid : naughtyApp) {
            pendingHandleList[uid] = 0; 
            tmp.append(' ').append(managedApp[uid].label.c_str());
        }
        if (naughtyApp.size()) {
            naughtyApp.clear();
            freezeit.log(string_view(tmp.c_str(), tmp.length));
        }
        else {
            freezeit.log("开机冻结 目前均处于冻结状态");
        }

        END_TIME_COUNT;
    }

    bool mountFreezerV1() {
        if (!access("/dev/jark_freezer", F_OK)) // 已挂载
            return true;

        // https://man7.org/linux/man-pages/man7/cgroups.7.html
        // https://www.kernel.org/doc/Documentation/cgroup-v1/freezer-subsystem.txt
        // https://www.containerlabs.kubedaily.com/LXC/Linux%20Containers/The-cgroup-freezer-subsystem.html

        mkdir("/dev/jark_freezer", 0666);
        mount("freezer", "/dev/jark_freezer", "cgroup", 0, "freezer");
        usleep(1000 * 100);
        mkdir("/dev/jark_freezer/frozen", 0666);
        mkdir("/dev/jark_freezer/unfrozen", 0666);
        usleep(1000 * 100);
        Utils::writeString("/dev/jark_freezer/frozen/freezer.state", "FROZEN");
        Utils::writeString("/dev/jark_freezer/unfrozen/freezer.state", "THAWED");

        // https://www.spinics.net/lists/cgroups/msg24540.html
        // https://android.googlesource.com/device/google/crosshatch/+/9474191%5E%21/
        Utils::writeString("/dev/jark_freezer/frozen/freezer.killable", "1"); // 旧版内核不支持
        usleep(1000 * 100);

        return (!access(cgroupV1FrozenPath, F_OK) && !access(cgroupV1UnfrozenPath, F_OK));
    }

    bool checkFreezerV2UID() {
        specialPlanV2Uid = !access(cgroupV2FreezerOppoCheckPath, F_OK);
        return (!access(cgroupV2FreezerCheckPath, F_OK) || specialPlanV2Uid);
    }

    bool checkReKernel() const  {
        return (!access("/proc/rekernel/", F_OK));
    }


    bool checkFreezerV2FROZEN() {
        return (!access(cgroupV2frozenCheckPath, F_OK) && !access(cgroupV2unfrozenCheckPath, F_OK));
    }

    void checkAndMountV2() {
        // https://cs.android.com/android/kernel/superproject/+/common-android12-5.10:common/kernel/cgroup/freezer.c

        if (checkFreezerV2UID())
            freezeit.log("原生支持 FreezerV2(UID)");

        if (checkFreezerV2FROZEN()) {
            freezeit.log("原生支持 FreezerV2(FROZEN)");
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
                freezeit.logFmt("设置%s FreezerV2(FROZEN)", fd > 0 ? "成功" : "失败");

                fd = open(cgroupV2unfrozenCheckPath, O_WRONLY | O_TRUNC);
                if (fd > 0) {
                    write(fd, "0", 2);
                    close(fd);
                }
                freezeit.logFmt("设置%s FreezerV2(UNFROZEN)", fd > 0 ? "成功" : "失败");

                freezeit.log("现已支持 FreezerV2(FROZEN)");
            }
        }
    }

    void printProcState() {
        START_TIME_COUNT;

        DIR* dir = opendir("/proc");
        if (dir == nullptr) {
            freezeit.logFmt("错误: %s(), [%d]:[%s]\n", __FUNCTION__, errno, strerror(errno));
            return;
        }

        //int getSignalCnt = 0;
        int totalMiB = 0;
        set<int> uidSet, pidSet;

        lock_guard<mutex> lock(naughtyMutex);
        naughtyApp.clear();

        stackString<1024 * 16> stateStr("进程冻结状态:\n\n PID | MiB |  状 态  | 进 程\n");

        struct dirent* file;
        while ((file = readdir(dir)) != nullptr) {
            if (file->d_type != DT_DIR || file->d_name[0] < '0' || file->d_name[0] > '9') continue;

            const int pid = Fastatoi(file->d_name);
            if (pid <= 100) continue;

            const size_t len = Faststrlen(file->d_name);
            char fullPath[64] = "/proc/";
            memcpy(fullPath + 6, file->d_name, len);

            struct stat statBuf;
            if (stat(fullPath, &statBuf))continue;
            const int uid = statBuf.st_uid;
            if (!managedApp.contains(uid)) continue;

            auto& appInfo = managedApp[uid];
            if (appInfo.isWhitelist()) continue;

            memcpy(fullPath + len + 6, "/cmdline", 9);
            char readBuff[256]; // now is cmdline Content
            if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
            const auto& package = appInfo.package;
            if (strncmp(readBuff, package.c_str(), package.length())) continue;
            const char endChar = readBuff[package.length()]; // 特例 com.android.chrome_zygote 无法binder冻结
            if (endChar != ':' && endChar != 0)continue;

            uidSet.insert(uid);
            pidSet.insert(pid);

            stackString<256> label(appInfo.label.c_str(), appInfo.label.length());
            if (readBuff[appInfo.package.length()] == ':')
                label.append(readBuff + appInfo.package.length());

            memcpy(fullPath + 6, file->d_name, len);
            memcpy(fullPath + len + 6, "/statm", 7);
            Utils::readString(fullPath, readBuff, sizeof(readBuff)); // now is statm content
            const char* ptr = strchr(readBuff, ' ');

            // Unit: 1 page(4KiB) convert to MiB. (Fastatoi(ptr) * 4 / 1024)
            const int memMiB = ptr ? (Fastatoi(ptr + 1) >> 8) : 0;
            totalMiB += memMiB;

            if (appInfo.isAudioPlaying) {
                stateStr.appendFmt("%5d %4d 🎵正在播放 %s\n", pid, memMiB, label.c_str());
                continue;
            }

            if (curForegroundApp.contains(uid)) {
                stateStr.appendFmt("%5d %4d 📱正在前台 %s\n", pid, memMiB, label.c_str());
                continue;
            }

            if (pendingHandleList.contains(uid)) {
                const auto secRemain = pendingHandleList[uid];
                if (secRemain < 60)
                    stateStr.appendFmt("%5d %4d ⏳%d秒后冻结 %s\n", pid, memMiB, secRemain, label.c_str());
                else
                    stateStr.appendFmt("%5d %4d ⏳%d分后冻结 %s\n", pid, memMiB, secRemain / 60, label.c_str());
                continue;
            }

            memcpy(fullPath + 6, file->d_name, len);
            memcpy(fullPath + len + 6, "/wchan", 7);
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
            else if (!strcmp(readBuff, pStopwchan)) {
                stateStr.appendFmt("🧊ST冻结中(ptrace_stop) %s\n", label.c_str());
            }
            else if (!strcmp(readBuff, binder_wchan)) {
                stateStr.appendFmt("⚠️运行中(Binder通信) %s\n", label.c_str());
                naughtyApp.insert(uid);
            }
            else if (!strcmp(readBuff, pipe_wchan)) {
                stateStr.appendFmt("⚠️运行中(管道通信) %s\n", label.c_str());
                naughtyApp.insert(uid);
            }
            else if (!strcmp(readBuff, epoll_wait1_wchan) || !strcmp(readBuff, epoll_wait2_wchan)) {
                stateStr.appendFmt("⚠️运行中(就绪态) %s\n", label.c_str());
                naughtyApp.insert(uid);
            }
            else {
                stateStr.appendFmt("⚠️运行中(%s) %s\n", (const char*)readBuff, label.c_str());
                naughtyApp.insert(uid);
            }
        }
        closedir(dir);

        if (uidSet.empty()) {
            freezeit.log("后台很干净，一个黑名单应用都没有");
        }
        else {
            if (!naughtyApp.empty()) {
                stateStr.append("\n 发现 [未冻结状态] 的进程, 即将进行冻结\n");
                for (const int uid : naughtyApp) 
                    pendingHandleList[uid] = 1;
            }

            stateStr.appendFmt("\n总计 %d 应用 %d 进程, 占用内存 ", (int)uidSet.size(), (int)pidSet.size());
            stateStr.appendFmt("%.2f GiB", totalMiB / 1024.0);

            freezeit.log(string_view(stateStr.c_str(), stateStr.length));
        }

        END_TIME_COUNT;
    }

    // 解冻新APP, 旧APP加入待冻结列队
    void updateAppProcess() {
        bool isupdate = false;
        vector<int> newShowOnApp, toBackgroundApp;

        for (const int uid : curForegroundApp)
            if (!lastForegroundApp.contains(uid))
                newShowOnApp.emplace_back(uid);

        for (const int uid : lastForegroundApp)
            if (!curForegroundApp.contains(uid))
                toBackgroundApp.emplace_back(uid);

        if (newShowOnApp.size() || toBackgroundApp.size())
            lastForegroundApp = curForegroundApp;
        else
            return;

        for (const int uid : newShowOnApp) {
            // 如果在待冻结列表则只需移除
            if (pendingHandleList.erase(uid)) {
                isupdate = true;
                continue;
            }

            // 更新[打开时间]  并解冻
            auto& appInfo = managedApp[uid];
            appInfo.startTimestamp = time(nullptr);

            const int num = handleProcess(appInfo, false);
            if (num > 0) freezeit.logFmt("☀️解冻 %s %d进程", appInfo.label.c_str(), num);
            else freezeit.logFmt("😁启动 %s", appInfo.label.c_str());
        }

        for (const int uid : toBackgroundApp) { // 更新倒计时
            isupdate = true;
            managedApp[uid].delayCnt = 0;
            pendingHandleList[uid] = managedApp[uid].isTerminateMode() ?
                settings.terminateTimeout : settings.freezeTimeout;
        }

        if (isupdate)
            updatePendingByLocalSocket();
    }

    // 处理待冻结列队 call once per 1sec
    void processPendingApp() {
        bool isupdate = false;

        auto it = pendingHandleList.begin();
        while (it != pendingHandleList.end()) {
            auto& remainSec = it->second;
            if (--remainSec > 0) {//每次轮询减一
                it++;
                continue;
            }

            const int uid = it->first;
            auto& appInfo = managedApp[uid];

            if (appInfo.isAudioPlaying) { // 不该被冻结
                it++;    
                continue;
            }

            if (appInfo.isWhitelist() && curForegroundApp.contains(uid)) { // 刚切换成白名单的和已经被冻结过的
                it = pendingHandleList.erase(it);
                continue;
            }

            int num = handleProcess(appInfo, true);
            if (num < 0) {
                if (appInfo.delayCnt >= 5) {
                    handleSignal(appInfo, SIGKILL);
                    freezeit.logFmt("%s:%d 已延迟%d次, 强制杀死", appInfo.label.c_str(), -num, appInfo.delayCnt);
                    num = 0;
                }
                else {
                    appInfo.delayCnt++;
                    remainSec = 15 << appInfo.delayCnt;
                    freezeit.logFmt("%s:%d Binder正在传输, 第%d次延迟, %d%s 后再冻结", appInfo.label.c_str(), -num,
                        appInfo.delayCnt, remainSec < 60 ? remainSec : remainSec / 60, remainSec < 60 ? "秒" : "分");
                    it++;
                    continue;
                }
            }
            it = pendingHandleList.erase(it);
            appInfo.delayCnt = 0;

            appInfo.stopTimestamp = time(nullptr);
            const int delta = appInfo.startTimestamp == 0 ? 0 :
                (appInfo.stopTimestamp - appInfo.startTimestamp);
            appInfo.startTimestamp = appInfo.stopTimestamp;
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
                freezeit.logFmt("%s冻结 %s %d进程 %s",
                    appInfo.isSignalMode() ? "🧊" : "❄️",
                    appInfo.label.c_str(), num, timeStr.c_str());
            else freezeit.logFmt("😭关闭 %s %s", appInfo.label.c_str(), timeStr.c_str());

            isupdate = true;
        }

        if (isupdate)
            updatePendingByLocalSocket();

    }


    void updatePendingByLocalSocket() {
        START_TIME_COUNT;

        int buff[64] = {};
        int uidCnt = 0;
        for (const auto& [uid, remainSec] : pendingHandleList) {
            buff[uidCnt++] = uid;
            if (uidCnt > 60)
                break;
        }

        const int recvLen = Utils::localSocketRequest(XPOSED_CMD::UPDATE_PENDING, buff,
            uidCnt * sizeof(int), buff, sizeof(buff));

        if (recvLen == 0) {
            freezeit.logFmt("%s() 工作异常, 请确认LSPosed中Frozen勾选系统框架, 然后重启", __FUNCTION__);
            END_TIME_COUNT;
            return;
        }
        else if (recvLen != 4) {
            freezeit.logFmt("%s() 返回数据异常 recvLen[%d]", __FUNCTION__, recvLen);
            if (recvLen > 0 && recvLen < 64 * 4)
                freezeit.logFmt("DumpHex: %s", Utils::bin2Hex(buff, recvLen).c_str());
            END_TIME_COUNT;
            return;
        }
        else if (static_cast<REPLY>(buff[0]) == REPLY::FAILURE) {
            freezeit.log("Pending更新失败");
        }
        freezeit.debugFmt("pending更新 %d", uidCnt);
        END_TIME_COUNT;
        return;
    }

    void checkWakeup() {
        timelineIdx = (timelineIdx + 1) & 0x0FFF; // [ %4096]
        const auto uid = unfrozenTimeline[timelineIdx];
        if (uid == 0) return;

        unfrozenTimeline[timelineIdx] = 0;//清掉时间线当前位置UID信息

        if (!managedApp.contains(uid)) return;

        auto& appInfo = managedApp[uid];
        if (appInfo.isSignalOrFreezer()) {
            const int num = handleProcess(appInfo, false);
            if (num > 0) {
                appInfo.startTimestamp = time(nullptr);
                pendingHandleList[uid] = settings.freezeTimeout;//更新待冻结倒计时
                freezeit.logFmt("☀️定时解冻 %s %d进程", appInfo.label.c_str(), num);
            }
            else {
                freezeit.logFmt("🗑️后台被杀 %s", appInfo.label.c_str());
            }
        }
        else {
            appInfo.timelineUnfrozenIdx = -1;
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
            //     userId=0 visible=true topActivity=ComponentInfo{com.ruanmei.ithome/com.ruanmei.ithome.ui.NewsInfoActivity}
            if (!line.starts_with("  taskId=")) continue;
            if (line.find("visible=true") == string::npos) continue;

            auto startIdx = line.find_last_of('{');
            auto endIdx = line.find_last_of('/');
            if (startIdx == string::npos || endIdx == string::npos || startIdx > endIdx) continue;

            const string& package = line.substr(startIdx + 1, endIdx - (startIdx + 1));
            if (!managedApp.contains(package)) continue;
            int uid = managedApp.getUid(package);
            if (managedApp[uid].isWhitelist()) continue;
            curForegroundApp.insert(uid);
        }

        if (curForegroundApp.size() >= (lastForegroundApp.size() + 3)) //有时系统会虚报大量前台应用
            curForegroundApp = lastForegroundApp;

        END_TIME_COUNT;
    }

    // 常规查询前台 只返回第三方, 剔除白名单/桌面
	void getVisibleAppByShellLRU() {
		START_TIME_COUNT;

		curForegroundApp.clear();
		const char* cmdList[] = { "/system/bin/dumpsys", "dumpsys", "activity", "lru", nullptr };
		VPOPEN::vpopen(cmdList[0], cmdList + 1, getVisibleAppBuff.get(), GET_VISIBLE_BUF_SIZE);

		stringstream ss;
		ss << getVisibleAppBuff.get();

		string line;
		getline(ss, line);

        auto getForegroundLevel = [](const char* ptr) {
            constexpr uint32_t levelInt[7] = { 0x20524550, 0x55524550, 0x20504f54, 0x504f5442,
                                                0x20534746, 0x53474642, 0x46504d49 };
            const uint32_t target = *((uint32_t*)ptr);
            for (int i = 2; i < 7; i++) {
                if (target == levelInt[i])
                    return i;
            }
            return 16;
        };

        int offset = freezeit.SDK_INT_VER == 29 ? 5 : 3;
        auto startStr = freezeit.SDK_INT_VER == 29 ? "    #" : "  #";
        getline(ss, line);
        if (!strncmp(line.c_str(), "  Activities:", 4)) {
            while (getline(ss, line)) {
                if (strncmp(line.c_str(), startStr, offset)) break;

                auto linePtr = line.c_str() + offset; 

                auto ptr = linePtr + (linePtr[2] == ':' ? 11 : 12);
                int level = getForegroundLevel(ptr);
                if (level < 2 || 6 < level) continue;

                ptr = strstr(line.c_str(), "/u0a");
                if (!ptr)continue;
                int uid = 10000 + Fastatoi(ptr + 4);
                if (!managedApp.contains(uid))
                    continue;

                auto& appInfo = managedApp[uid];

                if (appInfo.isWhitelist())continue;
                if ((level <= 3) || appInfo.isPermissive) curForegroundApp.insert(uid);

#if DEBUG_DURATION
                freezeit.logFmt("Legacy前台 %s:%d", appInfo.label.c_str(), level);
#endif
            }
        }
		END_TIME_COUNT;
	}

    void getVisibleAppByLocalSocket() {
        START_TIME_COUNT;

        int buff[64];
        int recvLen = Utils::localSocketRequest(XPOSED_CMD::GET_FOREGROUND, nullptr, 0, buff,
            sizeof(buff));

        int& UidLen = buff[0];
        if (recvLen <= 0) {
            freezeit.logFmt("%s() 工作异常, 请确认LSPosed中Frozen勾选系统框架, 然后重启", __FUNCTION__);
            END_TIME_COUNT;
            return;
        }
        else if (UidLen > 16 || (UidLen != (recvLen / 4 - 1))) {
            freezeit.logFmt("%s() 前台服务数据异常 UidLen[%d] recvLen[%d]", __FUNCTION__, UidLen, recvLen);
            freezeit.logFmt("DumpHex: %s", Utils::bin2Hex(buff, recvLen < 64 * 4 ? recvLen : 64 * 4).c_str());
            END_TIME_COUNT;
            return;
        }

        curForegroundApp.clear();

        for (int i = 1; i <= UidLen; i++) {
            int& uid = buff[i];
            if (managedApp.contains(uid)) curForegroundApp.insert(uid);
            else freezeit.logFmt("非法UID[%d], 可能是新安装的应用, 请点击右上角第一个按钮更新应用列表", uid);
        }

#if DEBUG_DURATION
        string tmp;
        for (auto& uid : curForegroundApp)
            tmp += " [" + managedApp[uid].label + "]";
        if (tmp.length())
            freezeit.logFmt("LOCALSOCKET前台%s", tmp.c_str());
        else
            freezeit.log("LOCALSOCKET前台 空");
#endif
        END_TIME_COUNT;
    }

    void getAudioByLocalSocket() {
        sleep(5); 

        while (true) {
            if (systemTools.isAudioPlaying) {
                int buff[24] = {};  

                int recvLen = Utils::localSocketRequest(XPOSED_CMD::GET_AUDIO, nullptr, 0, buff, 
                    sizeof(buff));

                if (recvLen <= 0) {
                    freezeit.logFmt("%s() 工作异常, 请确认LSPosed中Frozen是否已经勾选系统框架", __FUNCTION__);
                    return;
                }
                else if (recvLen < 4) {
                    freezeit.logFmt("%s() 返回数据异常 recvLen[%d]", __FUNCTION__, recvLen);
                    if (recvLen > 0 && recvLen < 64 * 4)
                        freezeit.logFmt("DumpHex: %s", Utils::bin2Hex(buff, recvLen).c_str());
                    return;
                }

                const int uidCount = (recvLen / 4) - 1; 

                currentAudioApp.clear();

                for (int i = 0; i < uidCount; ++i) {
                    int uid = buff[i];

                    if (!managedApp.contains(uid)) continue;
                            
                    auto& appInfo = managedApp[uid];
                    if (appInfo.isWhitelist()) continue;
                    
                    if (appInfo.isPermissive) {
                        if (appInfo.package == "com.ss.android.ugc.aweme" 
                                || appInfo.package == "com.ss.android.ugc.aweme.lite") continue;
                        appInfo.isAudioPlaying = true;
                        currentAudioApp.emplace_back(uid);
                    }
                }

                for (int lastUid : lastAudioApp) {
                    bool stillPlaying = false;
                    for (int curUid : currentAudioApp) {
                        if (curUid == lastUid) {
                            stillPlaying = true;
                            break;
                        }
                    }
                    
                    if (!stillPlaying) 
                        managedApp[lastUid].isAudioPlaying = false;
                }

                lastAudioApp = std::move(currentAudioApp); 
            }
            Utils::sleep_ms(500);
        }
    }

    void handlePendingIntent() {
        int buff[24] = {0};

        const int recvLen = Utils::localSocketRequest(XPOSED_CMD::GET_INTENT, nullptr, 0, buff, 
            sizeof(buff));
        
        if (recvLen <= 0) {
            freezeit.logFmt("%s() 工作异常, 请确认LSPosed中Frozen是否已经勾选系统框架", __FUNCTION__);
            return;
        }
        else if (recvLen < 4) {
            freezeit.logFmt("%s() 返回数据异常 recvLen[%d]", __FUNCTION__, recvLen);
            if (recvLen > 0 && recvLen < 64 * 4)
                freezeit.logFmt("DumpHex: %s", Utils::bin2Hex(buff, recvLen).c_str());
            return;
        }

        const int uidCount = (recvLen / 4) - 1; 

        for (int i = 0; i < uidCount; i++) {
            const int uid = buff[i];

            if (!managedApp.contains(uid))
                return;

            auto& appInfo = managedApp[uid];
            if (appInfo.isFreeze && !curForegroundApp.contains(uid) && !pendingHandleList.contains(uid)) {
                freezeit.logFmt("后台意图:[%s],将进行临时解冻", appInfo.label.c_str());
                unFreezerTemporary(uid);
            }
        }
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

    void cpuSetTriggerTask() {
        constexpr int TRIGGER_BUF_SIZE = 8192;

        const int fd = inotify_init();
        if (fd < 0) {
            fprintf(stderr, "同步事件: 0xB1 (1/3)失败: [%d]:[%s]", errno, strerror(errno));
            exit(-1);
        }

        //int watch_d = inotify_add_watch(inotifyFd,
        //    systemTools.SDK_INT_VER >= 33 ? cpusetEventPathA13
        //    : cpusetEventPathA12,
        //    IN_ALL_EVENTS);

        const int wd = inotify_add_watch(fd, cpusetEventPath, IN_ALL_EVENTS);

        if (wd < 0) {
            fprintf(stderr, "同步事件: 0xB1 (2/3)失败: [%d]:[%s]", errno, strerror(errno));
            exit(-1);
        }

        freezeit.log("初始化同步事件: 0xB1");

        char buf[TRIGGER_BUF_SIZE];

        while (read(fd, buf, TRIGGER_BUF_SIZE) > 0) {
            if (doze.isScreenOffStandby && doze.checkIfNeedToExit()) {
                curForegroundApp = std::move(curFgBackup); // recovery
                updateAppProcess();
            }
            else {
                getVisibleAppByLocalSocket();
                updateAppProcess(); // ~40us
            }
        }

        inotify_rm_watch(fd, wd);
        close(fd);

        freezeit.log("已退出监控同步事件: 0xB0");
    }

    int getReKernelPort() {
        auto dir = opendir("/proc/rekernel"); 
        if (dir == nullptr) return -1;

        int port = -1;
        struct dirent *file;
        while ((file = readdir(dir))) {
            if (file->d_name[0] == '.') continue;  

            if (file->d_name[1] == '2') {
                closedir(dir);
                return 22; 
            } else if (file->d_name[1] == '6') {
                closedir(dir);
                return 26;  
            }
            
            if (file->d_name[1] >= '3' && file->d_name[1] <= '5') {
                port = 20 + (file->d_name[1] - '0');
            }
        }

        closedir(dir);
        return port;  
    }

    // Binder事件 需要额外magisk模块: ReKernel
    void binderEventTriggerTask() {
        if (!settings.enableunFreezerTemporary) return;
        int skfd, ret;
        user_msg_info u_info{};
        socklen_t len;
        struct sockaddr_nl saddr {}, daddr{};
        constexpr const char umsg[] = "Hello! Re:Kernel!";
        constexpr const char str[] = "#proc_remove";

        if (!checkReKernel()) {
            freezeit.log("ReKernel未安装");
            return;
        }

        const int NETLINK_TEST = getReKernelPort();

        freezeit.logFmt("已连接至ReKernel: %d#100", NETLINK_TEST);

        struct nlmsghdr* nlh = (struct nlmsghdr*)malloc(NLMSG_SPACE(MAX_PLOAD));

        while (true) {
            skfd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_TEST);
            if (skfd == -1) {
                freezeit.log("ReKernel AF_NETLINK 创建失败");
                sleep(60);
                continue;
            }

            memset(&saddr, 0, sizeof(saddr));
            saddr.nl_family = AF_NETLINK;
            saddr.nl_pid = USER_PORT;
            saddr.nl_groups = 0;
            if (bind(skfd, (struct sockaddr*)&saddr, sizeof(saddr)) != 0) {
                close(skfd);

                freezeit.log("ReKernel bind 失败");
                sleep(60);
                continue;
            }

            memset(&daddr, 0, sizeof(daddr));
            daddr.nl_family = AF_NETLINK;
            daddr.nl_pid = 0;
            daddr.nl_groups = 0;

            memset(nlh, 0, sizeof(struct nlmsghdr));
            nlh->nlmsg_len = NLMSG_SPACE(MAX_PLOAD);
            nlh->nlmsg_flags = 0;
            nlh->nlmsg_type = 0;
            nlh->nlmsg_seq = 0;
            nlh->nlmsg_pid = saddr.nl_pid;

            memcpy(NLMSG_DATA(nlh), umsg, sizeof(umsg) - 1);
            //freezeit.logFmt("Send msg to kernel:%s", umsg);

            ret = sendto(skfd, nlh, nlh->nlmsg_len, 0, (struct sockaddr*)&daddr, sizeof(struct sockaddr_nl));
            if (!ret) {
                close(skfd);

                freezeit.log("ReKernel Failed send msg to kernel");
                sleep(60);
                continue;
            }

            memcpy(NLMSG_DATA(nlh), str, sizeof(str) - 1); 
        
            ret = sendto(skfd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *)&daddr, sizeof(struct sockaddr_nl));
            if (!ret) freezeit.logFmt("通知ReKernel清理 /proc/rekernel/%d 节点失败", NETLINK_TEST);  
            
            while (true) {
                memset(&u_info, 0, sizeof(u_info));
                len = sizeof(struct sockaddr_nl);
                ret = recvfrom(skfd, &u_info, sizeof(user_msg_info), 0, (struct sockaddr*)&daddr, &len);
                if (!ret) {
                    freezeit.log("ReKernel Failed recv msg from kernel!");
                    break;
                }

                const char* isBinder = strstr(u_info.msg, "type=Binder");
                const char* isNetwork = strstr(u_info.msg, "type=Network");
                const char* targetUid = strstr(u_info.msg, "target=");

                if (isBinder) {
                    const char* ptr = strstr(u_info.msg, "oneway=");
                    const char* ptr2 = strstr(u_info.msg, "bindertype=free_buffer_full");
                    const int oneway = Fastatoi(ptr + 7);

                    if (oneway == 1 && ptr2 == nullptr) continue;

                    const int uid = Fastatoi(targetUid + 7);

                    if (!managedApp.contains(uid)) continue;
                        
                    auto& appInfo = managedApp[uid];

                    if (appInfo.isFreeze && !pendingHandleList.contains(uid) && !curForegroundApp.contains(uid)) {
                        unFreezerTemporary(uid);
                        freezeit.logFmt("[%s] 接收到Re:Kernel的Binder信息, 类别: %s 类型: 临时解冻, 将进行临时解冻",
                            managedApp[uid].label.c_str(), oneway ? "AYSNC" : "SYNC");  
                    }
                } else if (isNetwork && settings.enableNetWorkUnFreeze) {
                    const int uid = Fastatoi(targetUid + 7);

                    if (!managedApp.contains(uid)) continue;
                    auto& appInfo = managedApp[uid];

                    if (appInfo.isFreeze && !pendingHandleList.contains(uid) && !curForegroundApp.contains(uid)) {
                        unFreezerTemporary(uid);
                        freezeit.logFmt("[%s] 接收到Re:Kernel的网络信息, 类型: 网络解冻, 将进行临时解冻",
                            managedApp[uid].label.c_str());  
                    }
                }
            }  
        }
        close(skfd);
        free(nlh);
    }


    void cycleThreadFunc() {

        sleep(1);
        getVisibleAppByShell(); // 获取桌面

        while (true) {
            Utils::sleep_ms(1000);
        
            systemTools.cycleCnt++;

            processPendingApp();//1秒一次

            // 2分钟一次 在亮屏状态检测是否已经息屏  息屏状态则检测是否再次强制进入深度Doze
            if (doze.checkIfNeedToEnter()) {
                curFgBackup = std::move(curForegroundApp); //backup
                updateAppProcess();
                //setWakeupLockByLocalSocket(WAKEUP_LOCK::IGNORE); //TODO xposed端改为一律禁止
            }

            if (doze.isScreenOffStandby)continue;// 息屏状态 不用执行 以下功能
            handlePendingIntent();
            systemTools.checkBattery();// 1分钟一次 电池检测
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
            freezeit.log(errTips);
            return;
        }

        struct dirent* file;
        while ((file = readdir(dir)) != nullptr) {
            if (file->d_type != DT_DIR || file->d_name[0] < '0' || file->d_name[0] > '9') continue;

            const int pid = Fastatoi(file->d_name);
            if (pid <= 100) continue;

            const size_t len = Faststrlen(file->d_name);
            char fullPath[64] = "/proc/";
            memcpy(fullPath + 6, file->d_name, len);

            struct stat statBuf;
            if (stat(fullPath, &statBuf))continue;
            const int uid = statBuf.st_uid;
            if (!managedApp.contains(uid) || managedApp[uid].isWhitelist())
                continue;

            memcpy(fullPath + len + 6, "/cmdline", 9);
            char readBuff[256];
            if (Utils::readString(fullPath, readBuff, sizeof(readBuff)) == 0)continue;
            const auto& package = managedApp[uid].package;
            if (strncmp(readBuff, package.c_str(), package.length())) continue;
            const char endChar = readBuff[package.length()]; // 特例 com.android.chrome_zygote 无法binder冻结
            if (endChar != ':' && endChar != 0)continue;

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
            freezeit.logFmt("%s() 工作异常, 请确认LSPosed中Frozen勾选系统框架, 然后重启", __FUNCTION__);
            END_TIME_COUNT;
            return 0;
        }
        else if (recvLen != 4) {
            freezeit.logFmt("%s() 返回数据异常 recvLen[%d]", __FUNCTION__, recvLen);
            if (recvLen > 0 && recvLen < 64 * 4)
                freezeit.logFmt("DumpHex: %s", Utils::bin2Hex(buff, recvLen).c_str());
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

    // 0成功  小于0为操作失败的pid
    int handleBinder(appInfoStruct& appInfo, const bool freeze) {
        if (bs.fd <= 0)return 0;

        START_TIME_COUNT;

        // https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/drivers/android/binder.c;l=5434
        // 100ms 等待传输事务完成
        binder_freeze_info binderInfo{ .pid = 0u, .enable = freeze ? 1u : 0u, .timeout_ms = 0u };
        binder_frozen_status_info statusInfo = { 0, 0, 0 };

        if (freeze) { // 冻结
            for (size_t i = 0; i < appInfo.pids.size(); i++) {
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
                        freezeit.logFmt("冻结 Binder 发生异常 [%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
                        break;
                    }

                    // 解冻已经被冻结binder的进程
                    binderInfo.enable = 0;
                    for (size_t j = 0; j < i; j++) {
                        binderInfo.pid = appInfo.pids[j];

                        //TODO 如果解冻失败？
                        if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                            errorCode = errno;
                            freezeit.logFmt("撤消冻结：解冻恢复Binder发生错误：[%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
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
                    freezeit.logFmt("获取 [%s:%d] Binder 状态错误 ErrroCode:%d", appInfo.label.c_str(), statusInfo.pid, errorCode);
                }
                else if (statusInfo.sync_recv & 0b0010) { // 冻结后发现仍有传输事务
                    freezeit.logFmt("%s 仍有Binder传输事务", appInfo.label.c_str());

                    // 解冻全部进程
                    binderInfo.enable = 0;
                    for (size_t j = 0; j < appInfo.pids.size(); j++) {
                        binderInfo.pid = appInfo.pids[j];

                        //TODO 如果解冻失败？
                        if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                            int errorCode = errno;
                            freezeit.logFmt("撤消冻结：解冻恢复Binder发生错误：[%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
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
                    freezeit.logFmt("获取 [%s:%d] Binder 状态错误 ErrroCode:%d", appInfo.label.c_str(), statusInfo.pid, errorCode);
                }
                else {
                    // 注意各个二进制位差别
                    // https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/services/core/jni/com_android_server_am_CachedAppOptimizer.cpp;l=489
                    // https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/drivers/android/binder.c;l=5467
                    if (statusInfo.sync_recv & 1) {
                        freezeit.debugFmt("[%s:%d] 冻结期间存在 同步传输 Sync transactions, 杀掉进程", appInfo.label.c_str(), statusInfo.pid);
                        //TODO 要杀掉进程
                        hasSync.insert(statusInfo.pid);
                    }
                    if (statusInfo.async_recv & 1) {
                        freezeit.debugFmt("[%s:%d] 冻结期间存在 异步传输（不重要）", appInfo.label.c_str(), statusInfo.pid);
                    }
                    if (statusInfo.sync_recv & 0b0010) {
                        freezeit.debugFmt("[%s:%d] 冻结期间存在“未完成”传输（不重要）TXNS_PENDING", appInfo.label.c_str(), statusInfo.pid);
                    }
                }
            }


            if (!hasSync.empty()) {
                for (auto it = appInfo.pids.begin(); it != appInfo.pids.end();) {
                    if (hasSync.contains(*it)) {
                        freezeit.debugFmt("杀掉进程 pid: %d", *it);
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
                    freezeit.logFmt("解冻 Binder 发生异常 [%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);

                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "/proc/%d/cmdline", binderInfo.pid);
                        
                    freezeit.logFmt("cmdline:[%s]", Utils::readString(tmp).c_str());

                    if (access(tmp, F_OK)) {
                        freezeit.logFmt("进程已不在 [%s:%u] ", appInfo.label.c_str(), binderInfo.pid);
                    }
                    //TODO 再解冻一次，若失败，考虑杀死？
                    else if (ioctl(bs.fd, BINDER_FREEZE, &binderInfo) < 0) {
                        errorCode = errno;
                        freezeit.logFmt("重试解冻 Binder 发生异常 [%s:%u] ErrorCode:%d", appInfo.label.c_str(), binderInfo.pid, errorCode);
                    }
                }
            }
        }

        END_TIME_COUNT;
        return 0;
    }

    void binder_close() {
        munmap(bs.mapped, bs.mapSize);
        close(bs.fd);
        bs.fd = -1;
    }

    void binderInit(const char* driver) {
        bs.fd = open(driver, O_RDWR | O_CLOEXEC);
        if (bs.fd < 0) {
            freezeit.logFmt("Binder初始化失败 路径打开失败：[%s] [%d:%s]", driver, errno, strerror(errno));
            return;
        }

        struct binder_version b_ver { -1 };
        if ((ioctl(bs.fd, BINDER_VERSION, &b_ver) < 0) ||
            (b_ver.protocol_version != BINDER_CURRENT_PROTOCOL_VERSION)) {
            freezeit.logFmt("Binder初始化失败 binder版本要求: %d  本机版本: %d", BINDER_CURRENT_PROTOCOL_VERSION,
                b_ver.protocol_version);
            close(bs.fd);
            bs.fd = -1;
            return;
        }
        else {
            freezeit.logFmt("初始驱动 BINDER协议版本 %d", b_ver.protocol_version);
        }

        // https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/services/core/jni/com_android_server_am_CachedAppOptimizer.cpp;l=489
        binder_frozen_status_info info = { (uint32_t)getpid(), 0, 0 };
        if (ioctl(bs.fd, BINDER_GET_FROZEN_INFO, &info) < 0) {
            int ret = -errno;
            freezeit.logFmt("Binder初始化失败 不支持 BINDER_FREEZER 特性 ErrroCode:%d", ret);
            close(bs.fd);
            bs.fd = -1;
            return;
        }
        else {
            freezeit.log("特性支持 BINDER_FREEZER");
        }

        bs.mapped = mmap(NULL, bs.mapSize, PROT_READ, MAP_PRIVATE, bs.fd, 0);
        if (bs.mapped == MAP_FAILED) {
            freezeit.logFmt("Binder初始化失败 Binder mmap失败 [%s] [%d:%s]", driver, errno, strerror(errno));
            close(bs.fd);
            bs.fd = -1;
            return;
        }
    }    
};
