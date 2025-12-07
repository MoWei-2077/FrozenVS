$BOOTMODE || abort "- 🚫 安装失败，仅支持在 Magisk 或 KernelSU 下安装"

chmod a+x "$MODPATH"/Frozen
chmod a+x "$MODPATH"/service.sh

output=$(pm uninstall cn.myflv.android.noanr)
if [ "$output" == "Success" ]; then
    echo "- ⚠️功能冲突, 已卸载 [NoANR]"
fi

output=$(pm list packages cn.myflv.android.noactive)
if [ ${#output} -gt 2 ]; then
    echo "- ⚠️检测到 [NoActive](myflavor), 请到 LSPosed 将其禁用"
fi

output=$(pm list packages com.github.uissd.miller)
if [ ${#output} -gt 2 ]; then
    echo "- ⚠️检测到 [Miller](UISSD), 请到 LSPosed 将其禁用"
fi

output=$(pm list packages com.github.f19f.milletts)
if [ ${#output} -gt 2 ]; then
    echo "- ⚠️检测到 [MiTombstone](f19没有新欢), 请到 LSPosed 将其禁用"
fi

output=$(pm list packages com.ff19.mitlite)
if [ ${#output} -gt 2 ]; then
    echo "- ⚠️检测到 [Mitlite](f19没有新欢), 请到 LSPosed 将其禁用"
fi

output=$(pm list packages com.sidesand.millet)
if [ ${#output} -gt 2 ]; then
    echo "- ⚠️检测到 [SMillet](酱油一下下), 请到 LSPosed 将其禁用"
fi

output=$(pm list packages nep.timeline.freezer)
if [ ${#output} -gt 2 ]; then
    echo "- ⚠️检测到 [Freezer](Timeline), 请到 LSPosed 将其禁用"
fi

output=$(pm list packages com.mubei.android)
if [ ${#output} -gt 2 ]; then
    echo "- ⚠️检测到 [墓碑](离音), 请到 LSPosed 将其禁用"
fi

output=$(pm uninstall io.github.jark006.freezeit)
if [ "$output" == "Success" ]; then
     echo "- ⚠️检测到 [冻它](JARK006), 已卸载"
fi

if [ -e "/data/adb/modules/mubei" ]; then
    echo "- ⚠️已禁用 [自动墓碑后台](奋斗的小青年)"
    touch /data/adb/modules/mubei/disable
fi

if [ -e "/data/adb/modules/Hc_tombstone" ]; then
    echo "- ⚠️已禁用 [新内核墓碑](时雨星空/火柴)"
    touch /data/adb/modules/Hc_tombstone/disable
fi

ORG_appcfg="/data/adb/modules/Frozen/appcfg.txt"
ORG_applabel="/data/adb/modules/Frozen/applabel.txt"
ORG_settings="/data/adb/modules/Frozen/settings.db"

sleep 1

for path in $ORG_appcfg $ORG_applabel $ORG_settings; do
    if [ -e $path ]; then
        cp -f $path "$MODPATH"
    fi
done

echo "- ⚠️⚠️⚠️ 如果您是从6.17以前的版本更新的 请先卸载此模块再刷入 否则将可能导致模块工作异常 -⚠️⚠️⚠️"

output=$(pm list packages io.github.MoWei.Frozen)
if [ ${#output} -lt 2 ]; then
    echo "- ⚠️ 首次安装, 安装完毕后, 请到LSPosed管理器启用Frozen, 然后再重启"
fi

module_version="$(grep_prop version "$MODPATH"/module.prop)"
echo "- 正在安装 $module_version"

fullApkPath=$(ls "$MODPATH"/Frozen.apk)
apkPath=/data/local/tmp/Frozen.apk
mv -f "$fullApkPath" "$apkPath"
chmod 666 "$apkPath"

echo "- Frozen APP 正在安装..."
output=$(pm install -r -f "$apkPath" 2>&1)
if [ "$output" == "Success" ]; then
    echo "- Frozen APP 安装成功"
    rm -rf "$apkPath"
else
    echo "- Frozen APP 安装失败, 原因: [$output] 尝试卸载再安装..."
    pm uninstall io.github.MoWei.Frozen
    sleep 1
    output=$(pm install -r -f "$apkPath" 2>&1)
    if [ "$output" == "Success" ]; then
        echo "- Frozen APP 安装成功"
        echo "- ⚠️请到LSPosed管理器重新启用Frozen, 然后再重启"
        rm -rf "$apkPath"
    else
        apkPathSdcard="/sdcard/Frozen_${module_version}.apk"
        cp -f "$apkPath" "$apkPathSdcard"
        echo "*********************** !!!"
        echo "  Frozen APP 依旧安装失败, 原因: [$output]"
        echo "  请手动安装 [ $apkPathSdcard ]"
        echo "*********************** !!!"
    fi
fi
echo "********更新日志********"
