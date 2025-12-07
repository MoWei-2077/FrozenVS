#!/system/bin/sh
MODDIR=${0%/*}

has_bin() {
    bin=$1
    if type "$bin" > /dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

has_prop() {
  for prop_name in "$@"
    do
        prop_value=$(getprop "$prop_name" 2>/dev/null)
        if [ -n "$prop_value" ]; then
            return 0
        fi
    done
    return 1
}


# Hans
if has_bin "hans" ||  has_prop "persist.vendor.enable.hans"; then
    resetprop -n persist.vendor.enable.hans false
    resetprop -n sys.hans.enable true
fi

# Millet
if  has_bin "millet_monitor" || has_prop "persist.sys.gz.enable"; then
    resetprop -n sys.millet.monitor 1
    resetprop -n persist.sys.gz.enable false
    resetprop -n persist.sys.brightmillet.enable false
    resetprop -n persist.sys.powmillet.enable false
    resetprop -n persist.sys.millet.handshake true
fi


