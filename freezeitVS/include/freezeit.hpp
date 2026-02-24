#pragma once

#include "utils.hpp"
#include "vpopen.hpp"

class Freezeit {
private:
    constexpr static int LINE_SIZE = 1024 * 32;   //  32 KiB
    constexpr static int BUFF_SIZE = 1024 * 128;  // 128 KiB

    mutex logPrintMutex;

    size_t position = 0;
    char lineCache[LINE_SIZE] = "[00:00:00]  ";
    char logCache[BUFF_SIZE];

    static constexpr const char* propPath = "/data/adb/modules/Frozen/module.prop";

    uint8_t* deBugFlagPtr = nullptr;

    // "Jul 28 2022" --> "2022-07-28"
    const char compilerDate[12] = {
            __DATE__[7],
            __DATE__[8],
            __DATE__[9],
            __DATE__[10],// YYYY year
            '-',

            // First month letter, Oct Nov Dec = '1' otherwise '0'
            (__DATE__[0] == 'O' || __DATE__[0] == 'N' || __DATE__[0] == 'D') ? '1' : '0',

            // Second month letter Jan, Jun or Jul
            (__DATE__[0] == 'J') ? ((__DATE__[1] == 'a') ? '1'
            : ((__DATE__[2] == 'n') ? '6' : '7'))
            : (__DATE__[0] == 'F') ? '2'// Feb
            : (__DATE__[0] == 'M') ? (__DATE__[2] == 'r') ? '3' : '5'// Mar or May
            : (__DATE__[0] == 'A') ? (__DATE__[1] == 'p') ? '4' : '8'// Apr or Aug
            : (__DATE__[0] == 'S') ? '9'// Sep
            : (__DATE__[0] == 'O') ? '0'// Oct
            : (__DATE__[0] == 'N') ? '1'// Nov
            : (__DATE__[0] == 'D') ? '2'// Dec
            : 'X',

            '-',
            __DATE__[4] == ' ' ? '0' : __DATE__[4],// First day letter, replace space with digit
            __DATE__[5],// Second day letter
        '\0',
    };

    void toMem(const char* logStr, const int len) {
        if ((position + len) >= BUFF_SIZE)
            position = 0;

        memcpy(logCache + position, logStr, len);
        position += len;
    }

    static int propIndex(const char* key) {
        if (!strcmp(key, "id")) return 0;
        else if (!strcmp(key, "name")) return 1;
        else if (!strcmp(key, "version")) return 2;
        else if (!strcmp(key, "versionCode")) return 3;
        else if (!strcmp(key, "author")) return 4;
        else if (!strcmp(key, "description")) return 5;
        return -1;
    }

public:

    const char* prop[7] = {
        "Unknown", // id
        "Unknown", // name
        "Unknown", // version
        "0",       // versionCode
        "Unknown", // author
        "Unknown", // description
        nullptr
    };

    bool isSamsung{ false };
    bool isOppoVivo{ false };

    const char* moduleEnv = nullptr;

    Freezeit& operator=(Freezeit&&) = delete;

    Freezeit() {

        if (!access("/system/bin/magisk", F_OK)) 
            moduleEnv = "Magisk";
        else if (!access("/data/adb/ksud", F_OK)) 
            moduleEnv = "KernelSU";
        else if (!access("/data/adb/ap/bin/apd", F_OK)) 
			moduleEnv = "APatch";
		else 
            moduleEnv = "Unknown";

        auto fp = fopen(propPath, "r");
        if (!fp) {
            fprintf(stderr, "找不到模块属性文件 [%s]", propPath);
            exit(-1);
        }

        char tmp[1024 * 4];
        while (!feof(fp)) {
            fgets(tmp, sizeof(tmp), fp);
            if (!isalpha(tmp[0])) continue;
            tmp[sizeof(tmp) - 1] = 0;
            auto ptr = strchr(tmp, '=');
            if (!ptr)continue;

            *ptr = 0;
            for (size_t i = (ptr - tmp) + 1; i < sizeof(tmp); i++) {
                if (tmp[i] == '\n' || tmp[i] == '\r') {
                    tmp[i] = 0;
                    break;
                }
            }
            prop[propIndex(tmp)] = ptr;
        }
        fclose(fp);


        logFmt("模块版本 %s(%s)", prop[2], prop[3]);
        logFmt("编译时间 %s %s UTC+8", compilerDate, __TIME__);

        fprintf(stderr, "version %s", prop[2]); // 发送当前版本信息给监控进程

        char res[256];
        if (__system_property_get("gsm.operator.alpha", res) > 0 && res[0] != ',')
            logFmt("运营信息 %s", res);
        if (__system_property_get("gsm.network.type", res) > 0) logFmt("网络类型 %s", res);
        if (__system_property_get("ro.product.brand", res) > 0) {
            logFmt("设备厂商 %s", res);

            //for (int i = 0; i < 8; i++)res[i] |= 32;
            *((uint64_t*)res) |= 0x20202020'20202020ULL; // 转为小写
            if (!strncmp(res, "samsung", 7))
                isSamsung = true;
            else if (!strncmp(res, "oppo", 4) || !strncmp(res, "vivo", 4) ||
                !strncmp(res, "realme", 6) || !strncmp(res, "iqoo", 4))
                isOppoVivo = true;
        }
        if (__system_property_get("ro.product.marketname", res) > 0) logFmt("设备型号 %s", res);
        if (__system_property_get("persist.sys.device_name", res) > 0) logFmt("设备名称 %s", res);
        if (__system_property_get("ro.system.build.version.incremental", res) > 0)
            logFmt("系统版本 %s", res);
        if (__system_property_get("ro.soc.manufacturer", res) > 0 &&
            __system_property_get("ro.soc.model", res + 100) > 0)
            logFmt("硬件平台 %s %s", res, res + 100);
    }

    void setDebugPtr(uint8_t* ptr) {
        deBugFlagPtr = ptr;
    }

    bool isDebugOn() {
        if (deBugFlagPtr == nullptr)
            return false;

        return *deBugFlagPtr;
    }

    int formatTimePrefix() {
        time_t timeStamp = time(nullptr) + 8 * 3600L;
        int hour = (timeStamp / 3600) % 24;
        int min = (timeStamp % 3600) / 60;
        int sec = timeStamp % 60;

        //lineCache[LINE_SIZE] = "[00:00:00] ";
        lineCache[1] = (hour / 10) + '0';
        lineCache[2] = (hour % 10) + '0';
        lineCache[4] = (min / 10) + '0';
        lineCache[5] = (min % 10) + '0';
        lineCache[7] = (sec / 10) + '0';
        lineCache[8] = (sec % 10) + '0';

        return 11;
    }

    int formatTimeDebug() {
        time_t timeStamp = time(nullptr) + 8 * 3600L;
        int hour = (timeStamp / 3600) % 24;
        int min = (timeStamp % 3600) / 60;
        int sec = timeStamp % 60;

        //lineCache[LINE_SIZE] = "[00:00:00] DEBUG ";
        lineCache[1] = (hour / 10) + '0';
        lineCache[2] = (hour % 10) + '0';
        lineCache[4] = (min / 10) + '0';
        lineCache[5] = (min % 10) + '0';
        lineCache[7] = (sec / 10) + '0';
        lineCache[8] = (sec % 10) + '0';

        memcpy(lineCache + 11, "DEBUG ", 6);

        return 17;
    }

    void log(const char* str) {
        lock_guard<mutex> lock(logPrintMutex);

        const int prefixLen = formatTimePrefix();

        int strlen = Faststrlen(str);
        int len = strlen + prefixLen;
        memcpy(lineCache + prefixLen, str, strlen);

        lineCache[len++] = '\n';

        toMem(lineCache, len);
    }

    void log(const char* str, size_t strlen) {
        lock_guard<mutex> lock(logPrintMutex);

        const int prefixLen = formatTimePrefix();

        int len = strlen + prefixLen;
        memcpy(lineCache + prefixLen, str, strlen);

        lineCache[len++] = '\n';

        toMem(lineCache, len);
    }


    template<typename... Args>
    void logFmt(const char* fmt, Args&&... args) {
        lock_guard<mutex> lock(logPrintMutex);

        const int prefixLen = formatTimePrefix();

        int len = snprintf(lineCache + prefixLen, (size_t)(LINE_SIZE - prefixLen), fmt, std::forward<Args>(args)...) + prefixLen;

        if (len <= 11 || LINE_SIZE <= (len + 1)) {
            lineCache[11] = 0;
            fprintf(stderr, "日志异常: len[%d] lineCache[%s]", len, lineCache);
            return;
        }

        lineCache[len++] = '\n';

        toMem(lineCache, len);
    }

    void debug(const char* str) {
        if (!isDebugOn()) return;

        lock_guard<mutex> lock(logPrintMutex);

        const int prefixLen = formatTimeDebug();

        int strlen = Faststrlen(str);
        int len = strlen + prefixLen;
        memcpy(lineCache + prefixLen, str, strlen);

        lineCache[len++] = '\n';

        toMem(lineCache, len);
    }

    template<typename... Args>
    void debugFmt(const char* fmt, Args&&... args) {
        if (!isDebugOn())return;

        lock_guard<mutex> lock(logPrintMutex);

        const int prefixLen = formatTimeDebug();

        int len = FastSnprintf(lineCache + prefixLen, (size_t)(LINE_SIZE - prefixLen), fmt, std::forward<Args>(args)...) + prefixLen;

        if (len <= 13 || LINE_SIZE <= (len + 1)) {
            lineCache[13] = 0;
            fprintf(stderr, "日志异常: len[%d] lineCache[%s]", len, lineCache);
            return;
        }

        lineCache[len++] = '\n';

        toMem(lineCache, len);
    }

    void clearLog() {
        logCache[0] = '\n';
        position = 1;
    }

    char* getLogPtr() {
        return logCache;
    }

    size_t getLoglen() {
        return position;
    }
};
