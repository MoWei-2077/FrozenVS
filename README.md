# Frozen

**[面具模块]** 第三方安卓墓碑，实现堪比"IOS"类似的完美墓碑机制

### 开发说明

1. 在 `build_pack.ps1` 第18/19行 设置 NDK工具链路径 `$ndkPath` 和 zip打包路径 `$releaseDir`, 然后执行 `build_pack.ps1` 即可直接编译 (项目生成功能已设为执行 build_pack.ps1，所以通过菜单 “生成-重新生成freezeitVS” 即可进行编译)。
1. 其中 `$ndkPath` 可以使用第一步安装的组件，默认路径 `C:/AndroidNDK/`，也可以使用AndroidSDK里的NDK(如果你有的话)，也可以使用单独的NDK工具链（[下载地址](https://developer.android.com/ndk/downloads)）。


