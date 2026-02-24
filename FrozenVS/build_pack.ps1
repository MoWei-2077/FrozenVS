$ErrorActionPreference = 'Stop'

function log($logContent){
    Write-Host ("[{0}] {1}" -f (Get-Date), $logContent)
}

function abort($logContent){
    Write-Host ("[{0}] {1}" -f (Get-Date), $logContent) -ForegroundColor:Red 
    exit -1
}

$ndkPath = "C:/AndroidNDK/toolchains/llvm/prebuilt/windows-x86_64"
$clang = "$ndkPath/bin/clang++.exe"
$sysroot = "--sysroot=$ndkPath/sysroot"
$cppFlags = "-std=c++23 -static -s -O3 -Wall -Wextra -Wshadow -fno-exceptions -fno-rtti -DNDEBUG -fPIE"
$zipFile = "./magisk/magisk.zip"

log "编译中..."
$target = "--target=aarch64-none-linux-android29"
& $clang $target $sysroot $cppFlags.Split(' ') -Iinclude src/main.cpp -o magisk/Frozen
if (-not$?)
{
    abort "编译失败"
}

log "编译完成"

log "打包中...  $zipFile"
& ./7za.exe a "$zipFile" ./magisk/* | Out-Null
if (-not$?)
{
    abort "打包失败"
}
log "打包完成"
log "Everything OK"