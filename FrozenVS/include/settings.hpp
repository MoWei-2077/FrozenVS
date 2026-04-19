#pragma once

#include "utils.hpp"
#include "freezeit.hpp"

class Settings {
private:
    Freezeit& freezeit;
    mutex writeSettingMutex;

    string settingsPath;

    constexpr static size_t SETTINGS_SIZE = 256;
    uint8_t settingsVar[SETTINGS_SIZE] = {
            8,  //[0] 设置文件版本
            0,  //[1] 
            10, //[2] freezeTimeout sec
            4,  //[3] wakeupTimeoutIdx  定时唤醒 参数索引 0-5：关闭, 5m, 15m, 30m, 1h, 2h
            20, //[4] terminateTimeout sec
            0,  //[5] setMode 设置Freezer模式  0: v2(FROZEN)(默认), 1: v2(UID), 2: V1(FROZEN) 3: 全局SIGSTOP
            2,  //[6] refreezeTimeoutIdx 定时压制 参数索引 0-3：关闭, 30m, 1h, 2h
            0,  //[7]
            0,  //[8]
            0,  //[9]
            1,  //[10] 
            0,  //[11]
            0,  //[12]
            0,  //[13] 电池监控
            0,  //[14] 电流校准
            0,  //[15] 双电芯
            1,  //[16] 开机冻结
            0,  //[17] Binder冻结
            0,  //[18] 
            0,  //[19] 
            0,  //[20] 临时解冻
            0,  //[21] 全局断网
            0,  //[22] 调整 lmk 参数
            0,  //[23] 深度Doze
            0,  //[24] 网络解冻
            0,  //[25]
            0,  //[26]
            0,  //[27]
            0,  //[28]
            0,  //[29]
            0,  //[30] Doze调试日志
            0,  //[31]
            0,  //[32]
    };


    // 关闭, 30m, 1h, 2h
    static constexpr int refreezeTimeoutList[] = { 86400 * 365, 60 * 30, 3600, 3600 * 2 };
    // 最大索引
    static constexpr int refreezeTimeoutIdxMax = sizeof(refreezeTimeoutList) / sizeof(refreezeTimeoutList[0]) - 1;

    // 关闭, 5m, 15m, 30m, 1h, 2h
    static constexpr int wakeupTimeoutList[] = { 86400 * 365, 60 * 5, 60 * 15, 60 * 30, 3600, 3600 * 2 };
    // 最大索引
    static constexpr int wakeupTimeoutIdxMax = sizeof(wakeupTimeoutList) / sizeof(wakeupTimeoutList[0]) - 1;


public:
    uint8_t& settingsVer = settingsVar[0];                // 设置文件版本
    uint8_t& freezeTimeout = settingsVar[2];              // 超时冻结 单位 秒
    uint8_t& wakeupTimeoutIdx = settingsVar[3];           // 定时唤醒 参数索引 0-5：关闭, 5m, 15m, 30m, 1h, 2h
    uint8_t& terminateTimeout = settingsVar[4];           // 超时杀死 单位 秒
    uint8_t& setMode = settingsVar[5];                    // Freezer模式
    uint8_t& refreezeTimeoutIdx = settingsVar[6];         // 定时压制 参数索引 0-3：关闭, 30m, 1h, 2h

    uint8_t& enableBatteryMonitor = settingsVar[13];      // 电池监控
    uint8_t& enableCurrentFix = settingsVar[14];          // 电池电流校准
    uint8_t& enableDoubleCell = settingsVar[15];          // 双电芯 电流翻倍
    uint8_t& enableBootFreeze = settingsVar[16];          // 开机冻结
    uint8_t& enableBinderFreezer = settingsVar[17];       // Binder Freezer
    //uint8_t& enableMemoryReclaim = settingsVar[18];           // 内存回收
    //uint8_t& enableClearBettryWhllelist = settingsVar[19];    // 清理电池白名单

    uint8_t& enableunFreezerTemporary = settingsVar[20];  // 临时解冻
    uint8_t& enableBreakNetWork = settingsVar[21];        // 全局断网
    uint8_t& enableLMK = settingsVar[22];                 // 后台优化
    uint8_t& enableDoze = settingsVar[23];                // 深度Doze       
    uint8_t& enableNetWorkUnFreeze = settingsVar[24];                // 网络解冻 
    uint8_t& enableDebug = settingsVar[30];               // 调试日志

    Settings& operator=(Settings&&) = delete;

    Settings(Freezeit& freezeit) : freezeit(freezeit) {

        freezeit.setDebugPtr(settingsVar+30);

        settingsPath = freezeit.modulePath + "/settings.db";

        auto fd = open(settingsPath.c_str(), O_RDONLY);
        if (fd > 0) {
            uint8_t tmp[SETTINGS_SIZE] = { 0 };
            int readSize = read(fd, tmp, SETTINGS_SIZE);
            close(fd);

            if (readSize != SETTINGS_SIZE) {
                freezeit.log("设置文件校验失败, 将使用默认设置参数, 并更新设置文件");
                freezeit.logFmt("读取大小: %d Bytes.  要求大小: 256 Bytes.", readSize);
                freezeit.log(save() ? "⚙️设置成功" : "🔧设置文件写入失败");
            }
            else if (tmp[0] != settingsVer) {
                freezeit.logFmt("设置文件当前版本: V%d 要求版本: V%d，版本不兼容, 将使用新版默认设置参数, 请根据情况自行重新调整设置", 
                    static_cast<int>(tmp[0]), static_cast<int>(settingsVer));
                freezeit.log(save() ? "⚙️设置成功" : "🔧设置文件写入失败");
            }
            else {
                memcpy(settingsVar, tmp, SETTINGS_SIZE);

                bool isError = false;
                if (setMode > 3) {
                    isError = true;
                    setMode = 0;
                    freezeit.logFmt("冻结模式参数[%d]错误, 已重设为 FreezerV2 (FROZEN)", (int)setMode);
                }
                if (refreezeTimeoutIdx > refreezeTimeoutIdxMax) {
                    isError = true;
                    refreezeTimeoutIdx = 1;
                    freezeit.logFmt("定时压制参数[%d]错误, 已重设为 %d 分钟",
                        static_cast<int>(refreezeTimeoutIdx), refreezeTimeoutList[refreezeTimeoutIdx] / 60);
                }
                if (wakeupTimeoutIdx > wakeupTimeoutIdxMax) {
                    isError = true;
                    wakeupTimeoutIdx = 4;
                    freezeit.logFmt("定时解冻参数[%d]错误, 已重置为 %d 分钟",
                        static_cast<int>(wakeupTimeoutIdx), wakeupTimeoutList[wakeupTimeoutIdx] / 60);
                }
                if (freezeTimeout < 1 || 60 < freezeTimeout) {
                    isError = true;
                    freezeTimeout = 10;
                    freezeit.logFmt("超时冻结参数[%d]错误, 已重置为 %d 秒",
                        static_cast<int>(freezeTimeout), (int)freezeTimeout);
                }
                if (terminateTimeout < 3 || 120 < terminateTimeout) {
                    isError = true;
                    terminateTimeout = 30;
                    freezeit.logFmt("超时杀死参数[%d]错误, 已重置为 %d 秒",
                        static_cast<int>(terminateTimeout), (int)terminateTimeout);
                }
                if (isError) {
                    freezeit.log("新版本可能会调整部分设置，可能需要重新设置");
                    freezeit.log(save() ? "⚙️设置成功" : "🔧设置文件写入失败");
                }
            }
        }
        else {
            freezeit.log("设置文件不存在, 将初始化设置文件");
            freezeit.log(save() ? "⚙️设置成功" : "🔧设置文件写入失败");
        }
    }

    uint8_t& operator[](const int key) {
        return settingsVar[key];
    }

    const char* get() {
        return (const char*)settingsVar;
    }

    size_t size() const {
        return SETTINGS_SIZE;
    }

    bool isRefreezeEnable() const {
        return 0 < refreezeTimeoutIdx && refreezeTimeoutIdx <= refreezeTimeoutIdxMax;
    }
    int getRefreezeTimeout() const {
        return refreezeTimeoutList[refreezeTimeoutIdx <= refreezeTimeoutIdxMax ? refreezeTimeoutIdx : 0];
    }

    bool isWakeupEnable() const {
        return 0 < wakeupTimeoutIdx && wakeupTimeoutIdx <= wakeupTimeoutIdxMax;
    }
    int getWakeupTimeout() const {
        return wakeupTimeoutList[wakeupTimeoutIdx <= wakeupTimeoutIdxMax ? wakeupTimeoutIdx : 0];
    }

    bool save() {
        lock_guard<mutex> lock(writeSettingMutex);
        auto fd = open(settingsPath.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666);
        if (fd > 0) {
            int writeSize = write(fd, settingsVar, SETTINGS_SIZE);
            close(fd);
            if (writeSize == SETTINGS_SIZE)
                return true;

            freezeit.logFmt("设置异常, 文件实际写入[%d]Bytes", writeSize);
        }
        return false;
    }

    int checkAndSet(const int idx, const int val, char* replyBuf) {
        constexpr size_t REPLY_BUF_SIZE = 2048;

        switch (idx) {
        case 2: { // freezeTimeout sec
            if (val < 1 || 60 < val)
                return snprintf(replyBuf, REPLY_BUF_SIZE, "超时冻结参数错误, 欲设为:%d", val);
        }
              break;

        case 3: {  // wakeupTimeoutIdx
            if (val > wakeupTimeoutIdxMax)
                return snprintf(replyBuf, REPLY_BUF_SIZE, "定时解冻参数错误 欲设为:%d", val);
        }
              break;

        case 4: { // wakeupTimeoutIdx sec
            if (val < 3 || 120 < val)
                return snprintf(replyBuf, REPLY_BUF_SIZE, "超时杀死参数错误, 欲设为:%d", val);
        }
              break;

        case 5: { // setMode 0-1-2-3
            if (val > 3)
                return snprintf(replyBuf, REPLY_BUF_SIZE, "冻结模式参数错误, 欲设为:%d", val);
        }
              break;

        case 6: { // refreezeTimeoutIdx
            if (val > refreezeTimeoutIdxMax)
                return snprintf(replyBuf, REPLY_BUF_SIZE, "定时压制参数错误, 欲设为:%d", val);
        }
              break;

        case 10: // xxx
        case 11: // xxx
        case 12: // xxx
        case 13: // 电池监控
        case 14: // 电流校准
        case 15: // 双电芯
        case 16: // 开机冻结
        case 17: // Binder冻结
        case 18: // 内存回收
        case 19: // 清理电池白名单
        case 20: // 临时解冻
        case 21: // 全局断网
        case 22: // 后台优化
        case 23: // doze
        case 24: // 网络解冻
        case 25: //
        case 26: //
        case 27: //
        case 28: //
        case 29: //
        case 30: // 调试日志
        {
            if (val != 0 && val != 1)
                return snprintf(replyBuf, REPLY_BUF_SIZE, "开关值错误, 正常范围:0/1, 欲设为:%d", val);
        }
        break;

        default: {
            freezeit.logFmt("🔧设置失败，设置项不存在, [%d]:[%d]", idx, val);
            return snprintf(replyBuf, REPLY_BUF_SIZE, "设置项不存在, [%d]:[%d]", idx, val);
        }
        }

        settingsVar[idx] = val;
        if (save()) {
            return snprintf(replyBuf, REPLY_BUF_SIZE, "success");
        }
        else {
            freezeit.logFmt("🔧设置失败，写入设置文件失败, [%d]:%d", idx, val);
            return snprintf(replyBuf, REPLY_BUF_SIZE, "写入设置文件失败, [%d]:%d", idx, val);
        }
    }
};
