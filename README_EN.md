# ISP Image Viewer

English | [简体中文](README.md)

<p align="center">
  <img src="assets/brand/app_icon.png" width="128" alt="ISP Image Viewer icon">
</p>

ISP Image Viewer is a lightweight cross-platform desktop application built with Qt 6 for fast image browsing, pixel inspection, and synchronized multi-image comparison.

The project is currently in release-candidate stabilization. JPEG and PNG browsing and comparison are the primary release scope; headerless RAW/YUV and camera RAW support remain available as optional advanced capabilities.

## Highlights

- Finder/Explorer-style directory tree, history navigation, search, sorting, and thumbnail browsing
- Asynchronous directory scanning, image decoding, and persistent thumbnail caching
- GPU-backed image canvas built on Qt RHI
- Fit, 100%, cursor-centered zoom, pan, coordinate display, and RGBA pixel inspection
- Full-screen browsing and synchronized zoom/pan comparison for two to four images
- Horizontal or vertical split inspection for two images, plus hold-B-over-A comparison
- Per-pane file information, EXIF data, luma histogram, and pixel overlays
- One-to-four-pane multi-folder workspace with cross-directory selection
- Copy, cut, paste, rename, drag and drop, file-manager reveal, and system Trash integration
- Optional RAW/YUV interpretation, source-plane pixel inspection, histograms, and ROI statistics
- Optional camera RAW decoding, EXIF/IPTC/XMP metadata, and embedded ICC-to-sRGB conversion

## Supported formats

| Type | Support | Notes |
|---|---|---|
| JPEG / JPG | Built in | Browsing, thumbnails, full screen, and comparison |
| PNG | Built in | Browsing, thumbnails, full screen, and comparison with Alpha preserved |
| NV12 / NV21 / I420 / P010 | Built-in advanced feature | Headerless data; width, height, stride, and related parameters are required |
| Bayer RAW10 / RAW12 / RAW16 | Built-in advanced feature | CFA, valid bits, byte order, black/white levels, white balance, CCM, and gamma parameters |
| DNG / camera RAW | Optional | Requires LibRaw 0.21+ |
| EXIF / IPTC / XMP | Optional | JPEG/PNG metadata requires Exiv2 0.28+ |
| Embedded RGB ICC | Optional | Requires LittleCMS 2.x and converts into an sRGB display buffer |

TIFF, WebP, OpenEXR, HEIC/HEIF, AVIF, JPEG XL, PSD, SVG, PDF, and GIF are currently outside the supported scope.

## Requirements

- CMake 3.25+
- A C++20 compiler
- Qt 6.7+ with Core, Gui, Quick, Quick Controls 2, Quick Layouts, Svg, ShaderTools, and private Gui headers
- macOS: Apple Silicon; Qt 6.9.x is the currently validated version
- Windows: x64; MSYS2/UCRT64 with GCC and Ninja is recommended

Optional dependencies:

- LibRaw 0.21+
- Exiv2 0.28+
- LittleCMS 2.x

## Building

### macOS

Use the project wrapper:

```sh
./build_macos.sh dev --test
./build_macos.sh release
./build_macos.sh package
```

- `dev` creates a Debug application under `build/`
- `release` creates a Release application under `build/`
- `package` deploys runtime dependencies and writes the `.app` and ZIP to `dist/`; packaging also prunes unused Qt styles, QML modules, and plug-ins through a whitelist, then verifies that runtime dependencies are bundle-local

Equivalent CMake Preset commands:

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug --output-on-failure
```

Run `./build_macos.sh --help` for cleanup, signing, RHI validation, and parallel-build options.

For a smaller package, build the validated Qt 6.9 No-ICU variant:

```sh
./scripts/build_qt_no_icu_macos.sh -j 8
./build_macos.sh debug --qt-prefix build/qt-no-icu/install --test
./build_macos.sh package --qt-prefix build/qt-no-icu/install
```

This variant retains QML, SVG, international file names, and native natural sorting while
removing the packaged ICU libraries. The initial Qt build is long; later runs reuse
`build/qt-no-icu`. This custom Qt toolchain has currently been build-, test-, and
package-validated on macOS arm64 only. Windows continues to use the regular Qt build and
cannot reuse the macOS artifacts.

### Windows

The recommended toolchain is MSYS2/UCRT64:

```powershell
$env:MSYS2_UCRT64 = (& qmake -query QT_INSTALL_PREFIX).Trim()
.\build_windows.ps1 -Toolchain msys2 -Mode dev -Test
.\build_windows.ps1 -Toolchain msys2 -Mode release
.\build_windows.ps1 -Toolchain msys2 -Mode package
```

Windows package mode uses the same Qt runtime pruning policy as macOS. Platform-specific components remain separate: `qwindows` is retained on Windows and `qcocoa` on macOS.

Equivalent CMake Preset commands:

```powershell
cmake --preset windows-msys2-debug
cmake --build --preset windows-msys2-debug
ctest --preset windows-msys2-debug --output-on-failure
```

The wrapper also retains `-Toolchain msvc` as an optional Visual Studio path.

## Optional features

CMake enables an adapter automatically when its dependency is available. Each adapter can also be disabled explicitly:

```sh
cmake --preset macos-debug \
  -DISPVIEW_ENABLE_LIBRAW=OFF \
  -DISPVIEW_ENABLE_EXIV2=OFF \
  -DISPVIEW_ENABLE_LCMS2=OFF
```

The vcpkg manifest exposes these optional features:

- `camera-raw`
- `metadata-exiv2`
- `color-management`

The current GitHub Release workflow disables all three optional components and ships the core JPEG/PNG feature set. This keeps release packages smaller and isolates the licensing requirements of optional dependencies.

> **License note:** Exiv2 is licensed under GPL-2.0-or-later. Before enabling and distributing an Exiv2-backed build, make sure the complete distribution is compatible with that license. Qt, LibRaw, LittleCMS, and transitive packaged dependencies retain their respective licenses as well.

## Running

Launch the application normally, or pass an initial directory on the command line:

```sh
ISPImageViewer /path/to/images
```

Prebuilt packages are published through [GitHub Releases](https://github.com/L1xiaolong/IspImageViewer/releases). Current macOS packages may not be Apple-notarized; on first launch, you may need to Control-click the application in Finder and choose **Open**.

## Tests and benchmarks

Debug presets build the automated test suite:

```sh
ctest --preset macos-debug --output-on-failure
```

Release presets can build RAW decoding, histogram, color-management, and large-directory benchmarks:

```sh
cmake --build --preset macos-release
./build/macos-preset-release/tools/ispview_raw_benchmark --48mp
./build/macos-preset-release/tools/ispview_histogram_benchmark --48mp
./build/macos-preset-release/tools/ispview_color_benchmark --48mp
./build/macos-preset-release/tools/ispview_browser_benchmark --enforce
```

Real-world image fixtures should remain local. Do not commit media containing personal information, precise locations, or unclear ownership.

## Project layout

```text
src/core       Core types, caches, histograms, and synchronized state
src/io         Decoders, metadata, color management, and file operations
src/render     RHI rendering parameters and shaders
src/browser    Directory, thumbnail, drag-and-drop, and clipboard models
src/platform   macOS/Windows platform services and shortcuts
src/qml        Application entry point, controllers, and QML UI
tests          C++ and QML automated tests
tools          Benchmarks and diagnostic tools
```

## License

The project-level open-source license is still being prepared. Until a formal `LICENSE` file is added at the repository root, the repository remains under default copyright and does not grant permission to copy, modify, or redistribute its contents.

Third-party components are not covered by the future project license. Distributors must independently comply with the licenses of Qt, Exiv2, LibRaw, LittleCMS, and all transitive dependencies.
