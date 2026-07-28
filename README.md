# ISP Image Viewer

[English](README_EN.md) | 简体中文

<p align="center">
  <img src="assets/brand/app_icon.png" width="128" alt="ISP Image Viewer 图标">
</p>

ISP Image Viewer 是一款基于 Qt 6 的轻量级跨平台桌面图片查看与对比工具，面向需要快速浏览、检查像素并同步比较多张图片的用户。

项目当前处于 Release Candidate 稳定化阶段。JPEG、PNG 浏览与对比是主要发布范围；无头 RAW/YUV 和相机 RAW 支持作为可选的高级能力保留。

## 功能亮点

- 类 Finder/Explorer 的目录树、历史导航、搜索、排序和缩略图浏览
- 异步目录扫描、图片解码和持久化缩略图缓存
- 基于 Qt RHI 的 GPU 图片画布
- Fit、100%、光标中心缩放、平移、坐标与 RGBA 像素检查
- 全屏浏览，以及 2～4 张图片同步缩放和平移对比
- 两图水平/垂直滑动分割和按住 B 覆盖 A 检查
- 每个对比窗格可独立显示文件信息、EXIF、亮度直方图和像素值
- 1～4 面板多文件夹工作区，可跨目录选择图片进入对比
- 文件复制、剪切、粘贴、重命名、拖放、系统定位和回收站集成
- 可选的 RAW/YUV 参数解释、源平面像素检查、直方图和 ROI 统计
- 可选的相机 RAW、EXIF/IPTC/XMP 元数据和 ICC 到 sRGB 转换

## 支持的格式

| 类型 | 支持情况 | 说明 |
|---|---|---|
| JPEG / JPG | 内置 | 浏览、缩略图、全屏和对比 |
| PNG | 内置 | 浏览、缩略图、全屏和对比，保留 Alpha |
| NV12 / NV21 / I420 / P010 | 内置高级功能 | 无头数据，需要提供宽高、步长等参数 |
| Bayer RAW10 / RAW12 / RAW16 | 内置高级功能 | 支持 CFA、有效位、字节序、黑白电平、白平衡、CCM 和 Gamma 参数 |
| DNG / 相机 RAW | 可选 | 需要 LibRaw 0.21+ |
| EXIF / IPTC / XMP | 可选 | JPEG/PNG 元数据读取需要 Exiv2 0.28+ |
| 嵌入式 RGB ICC | 可选 | 需要 LittleCMS 2.x，转换到 sRGB 显示缓冲区 |

TIFF、WebP、OpenEXR、HEIC/HEIF、AVIF、JPEG XL、PSD、SVG、PDF 和 GIF 当前不在支持范围内。

## 系统与构建要求

- CMake 3.25+
- 支持 C++20 的编译器
- Qt 6.7+：Core、Gui、Quick、Quick Controls 2、Quick Layouts、Svg、ShaderTools 及 Gui 私有头文件
- macOS：Apple Silicon；Qt 6.9.x 为当前验证版本
- Windows：x64；推荐 MSYS2/UCRT64 + GCC + Ninja

可选依赖：

- LibRaw 0.21+
- Exiv2 0.28+
- LittleCMS 2.x

## 构建

### macOS

使用项目脚本：

```sh
./build_macos.sh dev --test
./build_macos.sh release
./build_macos.sh package
```

- `dev`：在 `build/` 中生成 Debug 应用
- `release`：在 `build/` 中生成 Release 应用
- `package`：部署运行时依赖，并在 `dist/` 中生成 `.app` 和 ZIP；打包阶段会按白名单裁剪未使用的 Qt 样式、QML 模块和插件，并检查运行时依赖是否全部位于包内

也可以直接使用 CMake Preset：

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug --output-on-failure
```

运行 `./build_macos.sh --help` 可查看清理、签名、RHI 验证、并行任务数等选项。

如需进一步缩小包体，可以构建项目验证过的 Qt 6.9 No-ICU 变体：

```sh
./scripts/build_qt_no_icu_macos.sh -j 8
./build_macos.sh debug --qt-prefix build/qt-no-icu/install --test
./build_macos.sh package --qt-prefix build/qt-no-icu/install
```

该变体保留 QML、SVG、国际化文件名和原生自然排序，但不再打包 ICU。首次编译 Qt
耗时较长，后续会复用 `build/qt-no-icu`。目前这条自定义 Qt 构建链只在 macOS arm64
完成了编译、自动测试和打包验证；Windows 仍使用常规 Qt，不能直接复用 macOS 产物。

### Windows

推荐使用 MSYS2/UCRT64：

```powershell
$env:MSYS2_UCRT64 = (& qmake -query QT_INSTALL_PREFIX).Trim()
.\build_windows.ps1 -Toolchain msys2 -Mode dev -Test
.\build_windows.ps1 -Toolchain msys2 -Mode release
.\build_windows.ps1 -Toolchain msys2 -Mode package
```

Windows 的 `package` 模式采用与 macOS 相同的 Qt 运行时裁剪清单；平台插件仍分别保留 `qwindows` 和 `qcocoa`，不会强行统一操作系统专属组件。

等效的 CMake Preset 命令：

```powershell
cmake --preset windows-msys2-debug
cmake --build --preset windows-msys2-debug
ctest --preset windows-msys2-debug --output-on-failure
```

脚本仍保留 `-Toolchain msvc` 作为可选的 Visual Studio 构建路径。

## 可选功能

CMake 在发现依赖时会自动启用对应适配器，也可以显式关闭：

```sh
cmake --preset macos-debug \
  -DISPVIEW_ENABLE_LIBRAW=OFF \
  -DISPVIEW_ENABLE_EXIV2=OFF \
  -DISPVIEW_ENABLE_LCMS2=OFF
```

使用 vcpkg 时，可选择以下 manifest feature：

- `camera-raw`
- `metadata-exiv2`
- `color-management`

项目的 GitHub Release 构建目前关闭这三个可选组件，只发布基础 JPEG/PNG 功能，以缩小包体并隔离可选依赖的许可证要求。

> **许可证提示：** Exiv2 采用 GPL-2.0-or-later。启用并分发 Exiv2 的构建前，请确认整个分发方案与其许可证兼容。Qt、LibRaw、LittleCMS 及打包产生的传递依赖也各自保留原有许可证。

## 运行

直接启动应用，或在命令行传入一个初始目录：

```sh
ISPImageViewer /path/to/images
```

预编译版本发布在 [GitHub Releases](https://github.com/L1xiaolong/IspImageViewer/releases)。当前 macOS 包可能没有 Apple notarization，首次运行时可能需要在 Finder 中右键选择“打开”。

## 测试与性能工具

Debug Preset 会构建自动测试：

```sh
ctest --preset macos-debug --output-on-failure
```

Release Preset 可构建 RAW 解码、直方图、颜色管理和大目录浏览 benchmark：

```sh
cmake --build --preset macos-release
./build/macos-preset-release/tools/ispview_raw_benchmark --48mp
./build/macos-preset-release/tools/ispview_histogram_benchmark --48mp
./build/macos-preset-release/tools/ispview_color_benchmark --48mp
./build/macos-preset-release/tools/ispview_browser_benchmark --enforce
```

需要真实图片的测试数据应保存在本地，不应提交包含个人信息、精确位置或来源不明的素材。

## 项目结构

```text
src/core       核心数据结构、缓存、直方图和同步状态
src/io         解码器、元数据、颜色管理和文件操作
src/render     RHI 渲染参数与 Shader
src/browser    目录、缩略图、拖放与剪贴板模型
src/platform   macOS/Windows 平台服务与快捷键
src/qml        应用入口、控制器和 QML 界面
tests          C++ 与 QML 自动测试
tools          性能测试和诊断工具
```

## 许可证

项目级开源许可证正在整理中。在根目录加入正式 `LICENSE` 文件之前，本仓库内容仍受默认版权保护，不代表已经授予复制、修改或再分发权。

第三方组件不受未来项目许可证覆盖，分发者需要分别遵守 Qt、Exiv2、LibRaw、LittleCMS 及其传递依赖的许可证。
