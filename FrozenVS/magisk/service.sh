#!/system/bin/sh
MODDIR=${0%/*}

# This function is copied from [ Uperf@yc9559 ] module.
wait_until_login() {

    # in case of /data encryption is disabled
    while [ "$(getprop sys.boot_completed)" != "1" ]; do
        sleep 2.5s
    done

    # we doesn't have the permission to rw "/sdcard" before the user unlocks the screen
    # shellcheck disable=SC2039
    local test_file="/sdcard/Android/.PERMISSION_TEST_FREEZEIT"
    true >"$test_file"
    while [ ! -f "$test_file" ]; do
        sleep 0.25s
        true >"$test_file"
    done
    rm "$test_file"
}

wait_until_login

sleep 10 # 等待系统稳定
"$MODDIR"/Frozen
