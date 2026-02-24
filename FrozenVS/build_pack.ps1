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


log "构建中..."
$target = "--target=aarch64-none-linux-android29"
& $clang $target $sysroot $cppFlags.Split(' ') -Iinclude src/main.cpp -o magisk/Frozen
if (-not$?)
{
    abort "构建失败"
}

log "构建完成"
