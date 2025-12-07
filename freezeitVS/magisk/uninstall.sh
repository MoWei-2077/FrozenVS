#!/system/bin/sh

remove_Frozen(){

    pm uninstall io.github.MoWei.Frozen
    rm -rf /sdcard/Android/Frozen*
}

(remove_Frozen &)
