#!/system/bin/sh
MODDIR=${0%/*}

# Hans
resetprop -n persist.vendor.enable.hans false
resetprop -n sys.hans.enable true

# Millet
resetprop -n sys.millet.monitor 1
resetprop -n persist.sys.gz.enable false
resetprop -n persist.sys.brightmillet.enable false
resetprop -n persist.sys.powmillet.enable false
resetprop -n persist.sys.millet.handshake true



