$ErrorActionPreference = 'Stop'

function log
{
    [CmdletBinding()]
    Param
    (
        [Parameter(Mandatory = $true, Position = 0)]
        [string]$LogMessage
    )
    Write-Output ("[{0}] {1}" -f (Get-Date), $LogMessage)
}

$windowsToolchainsDir = "D:/Android-NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
$clang = "${windowsToolchainsDir}/clang++.exe"
$target = "--target=aarch64-none-linux-android29"
$sysroot = "--sysroot=D:/Android-NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
$cppFlags = "-std=c++23 -static -s -O3 -Wall -flto -Wextra -Wshadow -fno-exceptions -fno-rtti -DNDEBUG -fPIE"

log "构建中..."
& $clang $target $sysroot $cppFlags.Split(' ') -Iinclude src/main.cpp -o magisk/Frozen
if (-not$?)
{
    log "构建失败"
    exit
}
log "构建成功"
