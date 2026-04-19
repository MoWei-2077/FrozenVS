#pragma once

#include "utils.hpp"
#include "settings.hpp"
#include "freezeit.hpp"

class SystemTools {
private:
    Freezeit& freezeit;
    Settings& settings;

    thread sndThread;

    constexpr static uint32_t COLOR_E = 0XFF22BB44; // efficiency
    constexpr static uint32_t COLOR_M = 0XFFDD6622; // performance
    constexpr static uint32_t COLOR_P = 0XFF2266BB; // performance+
    constexpr static uint32_t COLOR_PRIME = 0XFF2238EE; // Prime

    constexpr static uint32_t COLOR_CLUSTER[6][8] = {
            {COLOR_E, COLOR_E, COLOR_E, COLOR_E, COLOR_PRIME, COLOR_PRIME, COLOR_PRIME, COLOR_PRIME}, // 44
            {COLOR_E, COLOR_E, COLOR_E, COLOR_E, COLOR_M, COLOR_M, COLOR_M, COLOR_PRIME}, // 431
            {COLOR_E, COLOR_E, COLOR_E, COLOR_E, COLOR_M, COLOR_M, COLOR_PRIME, COLOR_PRIME}, // 422
            {COLOR_E, COLOR_E, COLOR_E, COLOR_M, COLOR_M, COLOR_P, COLOR_P, COLOR_PRIME}, // 3221
            {COLOR_E, COLOR_E, COLOR_E, COLOR_E, COLOR_E, COLOR_E, COLOR_PRIME, COLOR_PRIME}, // 62
            {COLOR_E, COLOR_E, COLOR_M, COLOR_M, COLOR_M, COLOR_M, COLOR_M, COLOR_PRIME}, // 251
    };

public:
    // 各核心 曲线颜色 ABGR
    //const uint32_t* COLOR_CPU = COLOR_CLUSTER[0];
    static constexpr uint32_t COLOR_CPU[16] = {

        //#22B8DD, #22DDB8, #22DD6D, #92DD22,  #E6E61A,#E6BD1A, #E66B1A, ##E61A1A

        0xffddb822, 0xffb8dd22, 0xff6ddd22, 0xff22dd92, 0xff1ae6e6, 0xff1abde6, 0xff1a6be6, 0xff1a1ae6,
        0xffddb822, 0xffb8dd22, 0xff6ddd22, 0xff22dd92, 0xff1ae6e6, 0xff1abde6, 0xff1a6be6, 0xff1a1ae6,

    };
    int cpuCluster = 0;    // 44(4+4), 431(4+3+1), 62(6+2) ...
    int cpuCoreTotal = 0;  // 全部核心数量
    int cpuCoreOnline = 0; // 当前可用核心数量
    uint32_t cycleCnt = 0; // 核心循环计数，约每秒+1

    MemInfoStruct memInfo;

    int cpuTemperature = 0;
    int batteryWatt = 0;

    int cpuBucketIdx = 0; // 当前 循环索引 的位置
    static constexpr int maxBucketSize = 32; // CPU历史记录数量
    cpuRealTimeStruct cpuRealTimeSumary[maxBucketSize] = {};   // CPU总使用率
    cpuRealTimeStruct cpuRealTimeCore[maxBucketSize][16] = {}; // CPU各核心使用率  最多16核 最大 100%

    char cpuTempPath[256] = "/sys/class/thermal/thermal_zone0/temp";

    bool isAudioPlaying = false;
    // bool isMicrophoneRecording = false;

    uint32_t extMemorySize = 0; // MiB

    KernelVersionStruct kernelVersion;
    string kernelVerStr{ "Unknown" };

    SystemTools& operator=(SystemTools&&) = delete;

    SystemTools(Freezeit& freezeit, Settings& settings) :
        freezeit(freezeit), settings(settings) {

        utsname kernelInfo{};
        if (!uname(&kernelInfo)) {
            sscanf(kernelInfo.release, "%d.%d.%d", &kernelVersion.main, &kernelVersion.sub,
                &kernelVersion.patch);
            kernelVerStr = to_string(kernelVersion.main) + "." + to_string(kernelVersion.sub) + "." + to_string(kernelVersion.patch);
            freezeit.logFmt("内核版本 %d.%d.%d", kernelVersion.main, kernelVersion.sub, kernelVersion.patch);
        }
        else {
            Utils::printException(nullptr, 0, "无法获取内核版本", 24);
            exit(0);
        }

        getCpuTempPath();
        InitCPU();

        InitLMK();

        sndThread = thread(&SystemTools::sndThreadFunc, this);

        extMemorySize = getExtMemorySize();
    }

    size_t formatRealTime(int* ptr) {

        int i = 0;
        ptr[i++] = memInfo.totalRam;
        ptr[i++] = memInfo.availRam;
        ptr[i++] = memInfo.totalSwap;
        ptr[i++] = memInfo.freeSwap;

        for (int coreIdx = 0; coreIdx < cpuCoreTotal; coreIdx++)
            ptr[i++] = cpuRealTimeCore[cpuBucketIdx][coreIdx].freq;
        for (int coreIdx = 0; coreIdx < cpuCoreTotal; coreIdx++)
            ptr[i++] = cpuRealTimeCore[cpuBucketIdx][coreIdx].usage;

        ptr[i++] = cpuRealTimeSumary[cpuBucketIdx].usage;
        ptr[i++] = cpuTemperature;
        ptr[i] = batteryWatt;

        return 4L * 23;
    }

    uint32_t getExtMemorySize() {
        constexpr const char* filePathMIUI = "/data/extm/extm_file";
        constexpr const char* filePathCOS = "/data/nandswap/swapfile";

        struct stat statBuf { };
        if (!access(filePathMIUI, F_OK)) {
            stat(filePathMIUI, &statBuf);
            if (statBuf.st_size > 1024 * 1024L)
                return statBuf.st_size >> 20;// bytes -> MiB
        }
        else if (!access(filePathCOS, F_OK)) {
            stat(filePathCOS, &statBuf);
            if (statBuf.st_size > 1024 * 1024L)
                return statBuf.st_size >> 20;// bytes -> MiB
        }

        return 0;
    }


//    std::string GetProperty(const std::string& key, const std::string& default_value) {
//        std::string property_value;
//#if defined(__BIONIC__)
//        const prop_info* pi = __system_property_find(key.c_str());
//        if (pi == nullptr) return default_value;
//
//        __system_property_read_callback(pi,
//            [](void* cookie, const char*, const char* value, unsigned) {
//                auto property_value = reinterpret_cast<std::string*>(cookie);
//                *property_value = value;
//            },
//            &property_value);
//#else
//        auto it = g_properties.find(key);
//        if (it == g_properties.end()) return default_value;
//        property_value = it->second;
//#endif
//        // If the property exists but is empty, also return the default value.
//        // Since we can't remove system properties, "empty" is traditionally
//        // the same as "missing" (this was true for cutils' property_get).
//        return property_value.empty() ? default_value : property_value;
//    }

    // 不适合频繁查找
    int GetProperty(const char* key, char* res) {
        const prop_info* pi = __system_property_find(key); //如果频繁使用，建议缓存 对应Key的 prop_info
        if (pi == nullptr) {
            res[0] = 0;
            return -1;
        }

        __system_property_read_callback(pi,
            [](void* cookie, const char*, const char* value, unsigned) {
                if (value[0])
                    strncpy((char*)cookie, value, PROP_VALUE_MAX);
                else  ((char*)cookie)[0] = 0;
            },
            res);

        return res[0] ? 1 : -1;
    }

    int getScreenProperty() {
        static const prop_info* pi = nullptr;

        if (pi == nullptr) {
            pi = __system_property_find("debug.tracing.screen_state");
            if (pi == nullptr) {
                return -1;
            }
        }

        char res[PROP_VALUE_MAX] = { 0 };
        __system_property_read_callback(pi,
            [](void* cookie, const char*, const char* value, unsigned) {
                if (value[0])
                    strncpy((char*)cookie, value, PROP_VALUE_MAX);
                else  ((char*)cookie)[0] = 0;
            },
            res);

        return res[0] ? res[0] - '0' : -1;
    }

    void InitLMK() {
        if (!settings.enableLMK) return;

        // https://cs.android.com/android/platform/superproject/+/master:system/memory/lmkd/lmkd.cpp
        // https://source.android.com/devices/tech/perf/lmkd

        // page(1 page  = 4KB)
        // 18432:0,23040:100,27648:200,32256:250,55296:900,80640:950
        //  8192:0,12288:100,16384:200,32768:250,65536:900,96000:950
        //  4096:0,5120:100,8192:200,32768:250,65536:900,96000:950
        constexpr const char* lmkdParameter[] = {
                "ro.config.low_ram", "false",// A10
                "ro.lmk.use_psi", "true",
                "ro.lmk.use_minfree_levels", "false",
                "ro.lmk.low", "1001",
                "ro.lmk.medium", "1001",
                "ro.lmk.critical", "0",
                "ro.lmk.critical_upgrade", "false",   
                "ro.lmk.upgrade_pressure", "100",
                "ro.lmk.downgrade_pressure", "100",
                "ro.lmk.kill_heaviest_task", "false",
                "ro.lmk.kill_timeout_ms", "0", // 以下是A11以上的选项
                "ro.lmk.psi_partial_stall_ms", "200",
                "ro.lmk.psi_complete_stall_ms", "700",
                "ro.lmk.thrashing_limit", "30",
                "ro.lmk.thrashing_limit_decay", "50",
                "ro.lmk.swap_util_max", "100",
                "ro.lmk.swap_free_low_percentage", "10", // 将被遗弃的旧选项
                "ro.lmk.debug", "false", // 将被遗弃的旧选项
        };
        // const char* adj = "0,100,200,250,900,950"; //另有 0,1,2,4,9,12
        static constexpr const char minfree[] = "8192,12288,16384,32768,55296,80640";

        int len = 36;
        if (!access("/sys/module/lowmemorykiller/parameters", F_OK)) {
            len -= 2;

            if (!Utils::writeString("/sys/module/lowmemorykiller/parameters/enable_lmk",
                "1", 2))
                freezeit.log("调整lmk参数: 设置 enable_lmk 失败");
            if (!Utils::writeString("/sys/module/lowmemorykiller/parameters/minfree",
                minfree, sizeof(minfree)))
                freezeit.log("调整lmk参数: 设置 minfree 失败");
        }
        if (freezeit.moduleEnv == "Magisk") {
            string cmd;
            for (int i = 0; i < len; i += 2)
                cmd += string("magisk resetprop ") + lmkdParameter[i] + " " + lmkdParameter[i + 1] + ";";
            cmd += "sleep 1;lmkd --reinit";
            system(cmd.c_str());
            freezeit.log("更新参数 LMK");
        }
        else if (freezeit.moduleEnv == "KernelSU") {
            if (!access("/data/adb/ksu/resetprop", F_OK)) {
                string cmd;
                for (int i = 0; i < len; i += 2)
                    cmd += string("/data/adb/ksu/resetprop ") + lmkdParameter[i] + " " + lmkdParameter[i + 1] + ";";
                cmd += "sleep 1;lmkd --reinit";
                system(cmd.c_str());
                freezeit.log("更新参数 LMK");
            }
            else {
                freezeit.log("未找到 KSU resetprop");
            }
        }
        else if (freezeit.moduleEnv == "Apatch") {
			if (!access("/data/adb/ap/bin/resetprop", F_OK)) {
				string cmd;
				for (int i = 0; i < len; i += 2)
					cmd += string("/data/adb/ap/bin/resetprop") + lmkdParameter[i] + " " + lmkdParameter[i + 1] + ";";
				cmd += "sleep 1;lmkd --reinit";
				system(cmd.c_str());
				freezeit.log("更新参数 LMK");
			}
            else {
				freezeit.log("未找到 APatch resetprop");
			}
        }
    }

    void getCpuTempPath() {
        // 主板温度 /sys/class/thermal/thermal_message/board_sensor_temp
        for (int i = 0; i < 32; i++) {
            char path[256];
            snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", i);

            char type[64] = {};
            Utils::readString(path, type, sizeof(type));

            if (!strncmp(type, "soc_max", 6) || !strncmp(type, "mtktscpu", 8) ||
                !strncmp(type, "cpu", 3)) {
                snprintf(cpuTempPath, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
                break;
            }
        }
    }

    map<uint32_t, uint32_t> getCpuCluster() {
        map < uint32_t, uint32_t > freqMap;
        char path[] = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq";
        for (int coreIdx = 0; coreIdx < cpuCoreTotal; coreIdx++) {
            path[27] = '0' + coreIdx;
            freqMap[Utils::readInt(path)]++;
        }
        return freqMap;
    }


    int readBatteryWatt() {
        int voltage = Utils::readInt("/sys/class/power_supply/battery/voltage_now");
        int current = Utils::readInt("/sys/class/power_supply/battery/current_now");

        if (2'000'000 < voltage) voltage >>= 10; //2-10V 串并联  uV -> mV
        if (settings.enableCurrentFix) {
            if (abs(current) > 100'000) { // 如果单位是毫安，那么电流大于100A不现实，所以单位应该是微安，这种情况不能开启电流校准
                current /= 1000; // uA -> mA
                settings.enableCurrentFix = 0;
                freezeit.log("电流校准不应开启, 已自动关闭");
                freezeit.log(settings.save() ? "⚙️设置成功" : "🔧设置文件写入失败");
            }
        }
        else {
            current /= 1000; // uA -> mA
        }

        if (settings.enableDoubleCell)//双电芯
            current *= 2;

        return (voltage * current) / (freezeit.isSamsung ? 1000 : -1000);
    }

    void checkBattery() {
        constexpr int TIMEOUT = 60;
        static int secCnt = 58;
        static int lastCapacity = 0;
        static int lastMinute = 0;

        START_TIME_COUNT;

        if (settings.enableBatteryMonitor == 0 || (++secCnt < TIMEOUT))
            return;

        secCnt = 0;

        const int nowCapacity = Utils::readInt("/sys/class/power_supply/battery/capacity");
        if (lastCapacity == nowCapacity)
            return;

        if (lastCapacity == 0) { // 开机时
            lastCapacity = nowCapacity;
            lastMinute = static_cast<int>(time(nullptr) / 60);

            // 电池内核状态 字符串
            // https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/drivers/power/supply/power_supply_sysfs.c
            const int charge_full_design = Utils::readInt("/sys/class/power_supply/battery/charge_full_design");
            const int charge_full = Utils::readInt("/sys/class/power_supply/battery/charge_full");
            const int cycle_count = Utils::readInt("/sys/class/power_supply/battery/cycle_count");
            const int battery_soh = Utils::readInt("/sys/class/oplus_chg/battery/battery_soh");

            if (charge_full_design) {
                freezeit.logFmt("🔋电池 设计容量: %dmAh", charge_full_design / 1000);
                int health = 100 * charge_full / charge_full_design;
                if (40 < health && health <= 100) {
                    freezeit.logFmt("🔋电池 当前容量: %dmAh", charge_full / 1000);
                    freezeit.logFmt("🔋电池 健康程度: %d%%", health);
                }
            }

            if (40 < battery_soh && battery_soh <= 100)
                freezeit.logFmt("🔋电池 健康程度(内置): %d%%", battery_soh);

            if (cycle_count)
                freezeit.logFmt("🔋电池 循环次数: %d", cycle_count);

            freezeit.log("🔋电池 数据由系统提供, 仅供参考");
        }
        else {
            const int mWatt = abs(readBatteryWatt());
            const int nowMinute = static_cast<int>(time(nullptr) / 60);
            const int deltaMinute = nowMinute - lastMinute;
            const int deltaCapacity = nowCapacity - lastCapacity;
            const int temperature = Utils::readInt("/sys/class/power_supply/battery/temp");

            stackString<64> timeStr;
            if (deltaMinute >= 60)
                timeStr.appendFmt("%d时", deltaMinute / 60);
            timeStr.appendFmt("%d分钟", deltaMinute % 60);

            freezeit.logFmt("%s到 %d%%  %s%s了%d%%  %.2fw %.1f℃",
                deltaCapacity < 0 ? (deltaMinute == 1 ? "❗耗电" : "🔋放电") :
                ((mWatt > 20'000 || deltaCapacity >= 3) ? "⚡快充" : "🔌充电"),
                nowCapacity, *timeStr, deltaCapacity < 0 ? "用" : "充",
                abs(deltaCapacity), mWatt / 1e3, temperature / 1e1);

            lastMinute = nowMinute;
            lastCapacity = nowCapacity;
            
        }
        END_TIME_COUNT;
    }


    void InitCPU() {
        cpuCoreTotal = sysconf(_SC_NPROCESSORS_CONF);
        cpuCoreOnline = sysconf(_SC_NPROCESSORS_ONLN);
        freezeit.logFmt("全部核心 %d 可用核心 %d", cpuCoreTotal, cpuCoreOnline);
        if (cpuCoreTotal != cpuCoreOnline) {
            stackString<128> tips("当前离线核心 ");
            char tmp[64];
            for (int i = 0; i < cpuCoreTotal; i++) {
                snprintf(tmp, sizeof(tmp), "/sys/devices/system/cpu/cpu%d/online", i);
                auto fd = open(tmp, O_RDONLY);
                if (fd < 0)continue;
                read(fd, tmp, 1);
                close(fd);
                if (tmp[0] == '0')
                    tips.append('[').append(i).append(']');
            }
            freezeit.log(string_view(tips.c_str(), tips.length));
        }
        if (cpuCoreTotal > 32) {
            cpuCoreTotal = 32;
            freezeit.log("处理器大于32线程, 曲线表将只绘制前 32 线程使用率");
        }
        if (cpuCoreOnline > 32) {
            cpuCoreOnline = 32;
        }


        const auto res = getCpuCluster();
        cpuCluster = 0;
        for (const auto& [freq, num] : res)
            cpuCluster = cpuCluster * 10 + num;

        if (cpuCluster && res.size() < 10) {
            stackString<256> tmp("核心频率");
            for (const auto& [freq, cnt] : res)
                tmp.appendFmt(" %.2fGHz*%d", freq / (freq > 1e8 ? 1e9 : 1e6), cnt);
            freezeit.log(string_view(tmp.c_str(), tmp.length));
        }
        else {
            freezeit.logFmt("核心频率获取失败 cpuCluster %d size %d", cpuCluster, res.size());
        }
    }

    uint32_t drawChart(uint32_t* imgBuf, uint32_t height, uint32_t width) {
        START_TIME_COUNT;

        while (height * width > 1024 * 1024) {
            height /= 2;
            width /= 2;
        }

        const uint32_t imgSize = sizeof(uint32_t) * height * width;
        memset(imgBuf, 0, imgSize);
        const uint32_t imgHeight = height * 4 / 5; // 0.8;

        // ABGR
        constexpr uint32_t COLOR_BLUE = 0xBBFF8000;
        constexpr uint32_t COLOR_GRAY = 0x01808080;

        const uint32_t percent25 = (height / 5) * width;
        const uint32_t percent50 = (height * 2 / 5) * width;
        const uint32_t percent75 = (height * 3 / 5) * width;
        for (uint32_t x = 0; x < width; x++) { //横线
            imgBuf[percent25 + x] = COLOR_GRAY;
            imgBuf[percent50 + x] = COLOR_GRAY;
            imgBuf[percent75 + x] = COLOR_GRAY;
        }

        uint32_t line_x_pos[10]{ 0 };
        for (int i = 1; i < 10; ++i)
            line_x_pos[i] = width * i / 10;
        for (uint32_t y = 0; y < imgHeight; y++) { //中间竖线
            const uint32_t heightBase = width * y;
            for (int i = 1; i < 10; ++i)
                imgBuf[heightBase + line_x_pos[i]] = COLOR_GRAY;
        }

        // 横轴坐标 物理，虚拟，内存条的 起点 占用点 终点
        const uint32_t mem_x_pos[6] = {
                width * 5 / 100,
                width * 5 / 100 +
                (memInfo.totalRam ? (width * 4 / 10 * (memInfo.totalRam - memInfo.availRam) /
                                     memInfo.totalRam) : 0),
                width * 45 / 100,

                width * 55 / 100,
                width * 55 / 100 +
                (memInfo.totalSwap ? (width * 4 / 10 * (memInfo.totalSwap - memInfo.freeSwap) /
                                      memInfo.totalSwap) : 0),
                width * 95 / 100,
        };

        //内存 进度条
        if (memInfo.totalSwap == 0) { // 0.85
            for (uint32_t y = (height * 218) >> 8; y < height; y++) {
                const uint32_t heightBase = width * y;
                for (uint32_t x = mem_x_pos[0]; x < mem_x_pos[2]; x++)
                    imgBuf[heightBase + x] = x < mem_x_pos[1] ? COLOR_BLUE : COLOR_GRAY;
            }
        }
        else {
            for (uint32_t y = (height * 218) >> 8; y < height; y++) {
                const uint32_t heightBase = width * y;
                for (uint32_t x = mem_x_pos[0]; x < mem_x_pos[2]; x++)
                    imgBuf[heightBase + x] = x < mem_x_pos[1] ? COLOR_BLUE : COLOR_GRAY;
                for (uint32_t x = mem_x_pos[3]; x < mem_x_pos[5]; x++)
                    imgBuf[heightBase + x] = x < mem_x_pos[4] ? COLOR_BLUE : COLOR_GRAY;
            }
        }

        for (int coreIdx = 0; coreIdx < 8; coreIdx++) {
            for (int minuteIdx = 1; minuteIdx < maxBucketSize; minuteIdx++) {
                int y0 =
                    (100 - cpuRealTimeCore[(cpuBucketIdx + minuteIdx) & 0x1f][coreIdx].usage) *
                    imgHeight / 100;
                int y1 =
                    (100 - cpuRealTimeCore[(cpuBucketIdx + minuteIdx+1) & 0x1f][coreIdx].usage) *
                    imgHeight / 100;

                if (y0 <= 0) y0 = 1;
                else if ((uint32_t)y0 >= imgHeight) y0 = imgHeight - 1;

                if (y1 <= 0) y1 = 1;
                else if ((uint32_t)y1 >= imgHeight) y1 = imgHeight - 1;

                int x0 = (width * (minuteIdx - 1)) / 31;
                int x1 = (width * minuteIdx) / 31;
                int pointHighLast = y0;
                for (int x = x0; x < x1; x++) {
                    int deltaUp = (x - x0) * (x - x0), deltaDown = (x1 - x) * (x1 - x);
                    int pointHigh = y0 + (y1 - y0) * deltaUp / (deltaUp + deltaDown);

                    int h = pointHighLast < pointHigh ? pointHighLast : pointHigh;
                    const int maxH = pointHighLast > pointHigh ? pointHighLast : pointHigh;
                    for (; h <= maxH; h++)
                        imgBuf[width * h + x] = COLOR_CPU[coreIdx];

                    pointHighLast = pointHigh;
                }
            }
        }

        const uint32_t bottomLine = imgHeight * width;
        for (uint32_t x = 0; x < width; x++) { //上下横线
            imgBuf[x] = COLOR_BLUE;
            imgBuf[bottomLine + x] = COLOR_BLUE;
        }

        for (uint32_t y = 0; y < imgHeight; y++) { //两侧竖线
            imgBuf[width * y] = COLOR_BLUE;
            imgBuf[width * (y + 1) - 1] = COLOR_BLUE;
        }

        END_TIME_COUNT;
        return imgSize;
    }

    // https://rosettacode.org/wiki/Bitmap/Bresenham%27s_line_algorithm#C
    // Bresenham s_line_algorithm
    void drawLine(uint32_t* imgBuf, const uint32_t width, const uint32_t COLOR,
        int x0, int y0, const int x1, const int y1) {

        //delta x y,  step x y
        const int dx = abs(x1 - x0);
        const int dy = abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1;
        const int sy = y0 < y1 ? 1 : -1;
        int err = (dx > dy ? dx : -dy) / 2;

        while (true) {
            if (y0 == y1) {
                int minValue = x0 <= x1 ? x0 : x1;
                const int maxValue = x0 > x1 ? x0 : x1;
                for (; minValue <= maxValue; minValue++)
                    imgBuf[width * y0 + minValue] = COLOR;
                return;
            }
            else if (x0 == x1) {
                int minValue = y0 <= y1 ? y0 : y1;
                const int maxValue = y0 > y1 ? y0 : y1;
                for (; minValue <= maxValue; minValue++)
                    imgBuf[width * minValue + x0] = COLOR;
                return;
            }

            imgBuf[width * y0 + x0] = COLOR;

            const int e2 = err;
            if (e2 > -dx) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dy) {
                err += dx;
                y0 += sy;
            }
        }
    }

    void getCPU_realtime(const uint32_t availableMiB) {
        static char path[] = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq";

        static uint32_t jiffiesSumLastSumary = 0;
        static uint32_t jiffiesIdleLastSumary = 0;

        static uint32_t jiffiesSumLastCore[16] = {};
        static uint32_t jiffiesIdleLastCore[16] = {};

        struct sysinfo s_info;
        if (!sysinfo(&s_info)) {
            memInfo.totalRam = s_info.totalram >> 20; // convert to MiB
            memInfo.availRam = availableMiB;

            memInfo.totalSwap = s_info.totalswap >> 20;
            memInfo.freeSwap = s_info.freeswap >> 20;
        }

        cpuBucketIdx = (cpuBucketIdx + 1) % maxBucketSize;

        // read frequency
        for (int coreIdx = 0; coreIdx < cpuCoreTotal; coreIdx++) {
            path[27] = '0' + coreIdx;
            cpuRealTimeCore[cpuBucketIdx][coreIdx].freq = Utils::readInt(path) / 1000;// MHz
        }

        // read occupy
        auto fp = fopen("/proc/stat", "rb");
        if (fp) {
            char buff[256];
            uint32_t jiffiesList[8] = { 0 };

            while (true) {
                fgets(buff, sizeof(buff), fp);
                if (strncmp(buff, "cpu", 3))
                    break;

                int coreIdx = -1;
                if (buff[3] == ' ') // 总CPU数据
                    sscanf(buff + 4, "%u %u %u %u %u %u %u",
                        jiffiesList + 0, jiffiesList + 1, jiffiesList + 2, jiffiesList + 3,
                        jiffiesList + 4, jiffiesList + 5, jiffiesList + 6);
                else
                    sscanf(buff + 3, "%d %u %u %u %u %u %u %u", &coreIdx,
                        jiffiesList + 0, jiffiesList + 1, jiffiesList + 2, jiffiesList + 3,
                        jiffiesList + 4, jiffiesList + 5, jiffiesList + 6);

                if (coreIdx >= cpuCoreTotal) {
                    freezeit.logFmt("CPU核心 coreIdx:%d 超过核心数量 %d, 暂不支持", coreIdx, cpuCoreTotal);
                    break;
                }

                // user, nice, system, idle, iowait, irq, softirq
                uint32_t jiffiesSum = 0;
                for (int jiffIdx = 0; jiffIdx < 7; jiffIdx++)
                    jiffiesSum += jiffiesList[jiffIdx];

                uint32_t& jiffiesIdle = jiffiesList[3];

                if (coreIdx == -1) { // CPU 综合数据
                    if (jiffiesSumLastSumary == 0) {
                        jiffiesSumLastSumary = jiffiesSum;
                        jiffiesIdleLastSumary = jiffiesIdle;
                    }
                    else {
                        const uint32_t sumDelta = jiffiesSum - jiffiesSumLastSumary;
                        const uint32_t idleDelta = jiffiesIdle - jiffiesIdleLastSumary;
                        const int usage = (sumDelta == 0 || idleDelta > sumDelta) ? 0 :
                            (idleDelta == 0 ? 100 : (100 * (sumDelta - idleDelta) / sumDelta));

                        cpuRealTimeSumary[cpuBucketIdx].usage = usage;
                        jiffiesSumLastSumary = jiffiesSum;
                        jiffiesIdleLastSumary = jiffiesIdle;
                    }
                }
                else { // 各核心数据
                    if (jiffiesSumLastCore[coreIdx] == 0) {
                        jiffiesSumLastCore[coreIdx] = jiffiesSum;
                        jiffiesIdleLastCore[coreIdx] = jiffiesIdle;
                    }
                    else {
                        const uint32_t sumDelta = jiffiesSum - jiffiesSumLastCore[coreIdx];
                        const uint32_t idleDelta = jiffiesIdle - jiffiesIdleLastCore[coreIdx];
                        const int usage = (sumDelta == 0 || idleDelta > sumDelta) ? 0 :
                            (idleDelta == 0 ? 100 : (100 * (sumDelta - idleDelta) / sumDelta));

                        cpuRealTimeCore[cpuBucketIdx][coreIdx].usage = usage;
                        jiffiesSumLastCore[coreIdx] = jiffiesSum;
                        jiffiesIdleLastCore[coreIdx] = jiffiesIdle;
                    }
                }
            }
            fclose(fp);
        }

        cpuTemperature = Utils::readInt(cpuTempPath);
        batteryWatt = readBatteryWatt();
    }


    // 0获取失败 1失败 2成功
    int breakNetworkByLocalSocket(const int uid) {
        START_TIME_COUNT;

        int buff[64];
        const int recvLen = Utils::localSocketRequest(XPOSED_CMD::BREAK_NETWORK, &uid, 4, buff,
            sizeof(buff));

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



    // https://blog.csdn.net/meccaendless/article/details/80238997
    void sndThreadFunc() {
        constexpr int SND_BUF_SIZE = 8192;
        constexpr const char* sndPath = "/dev/snd";

        // const char *event_str[EVENT_NUM] =
        // {
        //     "IN_ACCESS",
        //     "IN_MODIFY",
        //     "IN_ATTRIB",
        //     "IN_CLOSE_WRITE",
        //     "IN_CLOSE_NOWRITE",
        //     "IN_OPEN",
        //     "IN_MOVED_FROM",
        //     "IN_MOVED_TO",
        //     "IN_CREATE",
        //     "IN_DELETE",
        //     "IN_DELETE_SELF",
        //     "IN_MOVE_SELF"
        // };

        sleep(2);

        char buf[SND_BUF_SIZE];

        const int inotifyFd = inotify_init();
        if (inotifyFd < 0) {
            fprintf(stderr, "同步事件: 0xC0 (1/2)失败 [%d]:[%s]", errno, strerror(errno));
            exit(-1);
        }

        const int watch_d = inotify_add_watch(inotifyFd, sndPath,
            IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE);
        if (watch_d < 0) {
            fprintf(stderr, "同步事件: 0xC0 (2/2)失败 [%d]:[%s]", errno, strerror(errno));
            exit(-1);
        }

        freezeit.log("初始化同步事件: 0xC0");

        int playbackDevicesCnt = 0;
        ssize_t readLen;

        while ((readLen = read(inotifyFd, buf, SND_BUF_SIZE)) > 0) {
            int readCnt{ 0 };
            while (readCnt < readLen) {
                inotify_event* event{ reinterpret_cast<inotify_event*>(buf + readCnt) };
                readCnt += sizeof(inotify_event) + event->len;

                if (strncmp(event->name, "pcm", 3) || Utils::lastChar(event->name + 4) != 'p')
                    continue;

                if ((event->mask) & IN_OPEN) {
                    playbackDevicesCnt++;
                }
                else if (event->mask & (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)) {
                    if (playbackDevicesCnt > 0)
                        playbackDevicesCnt--;
                }
            }
            isAudioPlaying = playbackDevicesCnt > 0;
            usleep(500 * 1000);
        }

        inotify_rm_watch(inotifyFd, watch_d);
        close(inotifyFd);

        fprintf(stderr, "同步事件: 0xC0 异常退出 [%d]:[%s]", errno, strerror(errno));
        exit(-1);
    }

};
