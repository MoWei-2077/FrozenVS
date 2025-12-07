$ErrorActionPreference = 'Stop'

function log($logContent){
    Write-Host ("[{0}] {1}" -f (Get-Date), $logContent)
}

function abort($logContent){
    Write-Host ("[{0}] {1}" -f (Get-Date), $logContent) -ForegroundColor:Red 
    exit -1
}


$clang = "C:/AndroidNDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"
$sysroot = "--sysroot=C:/AndroidNDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
$cppFlags = "-std=c++23 -static -s -O3 -Wall -Wextra -Wshadow -fno-exceptions -fno-rtti -DNDEBUG -fPIE"


log "Compiler... ARM64"
$target = "--target=aarch64-none-linux-android31"
& $clang $target $sysroot $cppFlags.Split(' ') -Iinclude src/main.cpp -o build/Frozen
if (-not$?)
{
    abort "Compiler ARM64 fail"
}

log "All done"
