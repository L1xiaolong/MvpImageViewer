#!/usr/bin/env bash
set -euo pipefail

print_usage() {
    cat <<'EOF'
Usage:
  ./build_macos.sh [dev|debug|release|package] [options]

Commands:
  dev, debug  Build a Debug app under build/ (default; never writes dist/).
  release     Build a Release app under build/ (never writes dist/).
  package     Build Release, deploy dependencies, sign, verify, and write dist/.

Examples:
  ./build_macos.sh dev --test
  ./build_macos.sh release -j 8
  ./build_macos.sh package
  ./build_macos.sh package --qt-prefix build/qt-no-icu/install
  ./build_macos.sh package --sign "Developer ID Application: Example (TEAMID)"

Options:
  --test       Run CTest when using a Debug build.
  --rhi        Run the native Metal RHI acceptance test.
  --clean      Remove the selected build directory before configuring.
  --no-crashpad  Disable macOS Crashpad when its pinned source is unavailable.
  --qt-prefix  Build with a custom Qt installation instead of the Qt on PATH.
  --sign ID    Code-sign a package with ID. Default is an ad-hoc local signature.
  --no-zip     Do not also create the optional portable ZIP in package mode.
  -j N         Parallel build jobs. Defaults to the local CPU count.
  -h,--help    Show this help.

Environment:
  ISPVIEW_GITHUB_REPOSITORY  GitHub owner/repository embedded in update links.

Outputs:
  dev/debug  build/macos-preset-debug/src/qml/MVPImageViewer.app
  release    build/macos-preset-release/src/qml/MVPImageViewer.app
  custom Qt  build/macos-custom-qt-<mode>/src/qml/MVPImageViewer.app
  package    dist/MVPImageViewer-<version>-macos-<arch>.dmg (plus an optional ZIP)
EOF
}

command_name="dev"
mode="debug"
run_tests=0
run_rhi=0
clean=0
create_zip=1
enable_crashpad="OFF"
sign_identity="${ISPVIEW_CODESIGN_IDENTITY:--}"
qt_prefix="${ISPVIEW_QT_PREFIX:-}"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 6)"

if [[ $# -gt 0 ]]; then
    case "$1" in
        dev|debug|Debug|DEBUG)
            command_name="dev"
            mode="debug"
            shift
            ;;
        release|Release|RELEASE)
            command_name="release"
            mode="release"
            shift
            ;;
        package|Package|PACKAGE)
            command_name="package"
            mode="release"
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
    esac
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test)
            run_tests=1
            shift
            ;;
        --rhi)
            run_rhi=1
            shift
            ;;
        --clean)
            clean=1
            shift
            ;;
        --no-crashpad)
            enable_crashpad="OFF"
            shift
            ;;
        --qt-prefix)
            [[ $# -ge 2 ]] || { echo "Missing directory after --qt-prefix" >&2; exit 2; }
            qt_prefix="$2"
            shift 2
            ;;
        --sign)
            [[ $# -ge 2 ]] || { echo "Missing identity after --sign" >&2; exit 2; }
            sign_identity="$2"
            shift 2
            ;;
        --no-zip)
            create_zip=0
            shift
            ;;
        -j)
            [[ $# -ge 2 ]] || { echo "Missing value after -j" >&2; exit 2; }
            jobs="$2"
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            print_usage >&2
            exit 2
            ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build_macos.sh must be run on macOS." >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

command -v brew >/dev/null 2>&1 || {
    echo "Homebrew is required for LibRaw and Exiv2; install both libraries first." >&2
    exit 1
}
brew_prefix="$(brew --prefix)"

preset="macos-${mode}"
if [[ -n "$qt_prefix" ]]; then
    [[ -d "$qt_prefix" ]] || {
        echo "Custom Qt prefix does not exist: $qt_prefix" >&2
        exit 2
    }
    qt_prefix="$(cd "$qt_prefix" && pwd)"
    [[ -f "$qt_prefix/lib/cmake/Qt6/Qt6Config.cmake" ]] || {
        echo "Custom Qt prefix is incomplete: $qt_prefix" >&2
        exit 2
    }
    export PATH="$qt_prefix/bin:$PATH"
    build_dir="$script_dir/build/macos-custom-qt-${mode}"
else
    build_dir="$script_dir/build/macos-preset-${mode}"
fi
built_app="$build_dir/src/qml/MVPImageViewer.app"

if [[ "$clean" -eq 1 ]]; then
    echo "Removing build directory: $build_dir"
    rm -rf "$build_dir"
fi

if [[ -n "$qt_prefix" ]]; then
    build_type="Debug"
    build_testing="ON"
    build_benchmarks="OFF"
    if [[ "$mode" == "release" ]]; then
        build_type="Release"
        build_testing="OFF"
        build_benchmarks="OFF"
    fi
    echo "Configuring with custom Qt: $qt_prefix"
    cmake -S "$script_dir" -B "$build_dir" -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DBUILD_TESTING="$build_testing" \
        -DISPVIEW_BUILD_BENCHMARKS="$build_benchmarks" \
        -DISPVIEW_ENABLE_CRASHPAD="$enable_crashpad" \
        -DISPVIEW_GITHUB_REPOSITORY="${ISPVIEW_GITHUB_REPOSITORY:-}" \
        -DCMAKE_PREFIX_PATH="$qt_prefix;$brew_prefix" \
        -DQt6_DIR="$qt_prefix/lib/cmake/Qt6"
    echo "Building custom Qt configuration (-j $jobs)"
    cmake --build "$build_dir" -j "$jobs"
else
    echo "Configuring preset: $preset"
    cmake --preset "$preset" \
        -DISPVIEW_BUILD_BENCHMARKS=OFF \
        -DCMAKE_PREFIX_PATH="$brew_prefix" \
        -DISPVIEW_ENABLE_CRASHPAD="$enable_crashpad" \
        -DISPVIEW_GITHUB_REPOSITORY="${ISPVIEW_GITHUB_REPOSITORY:-}"
    echo "Building preset: $preset (-j $jobs)"
    cmake --build --preset "$preset" -j "$jobs"
fi

if [[ ! -d "$built_app" ]]; then
    echo "Expected app bundle was not produced: $built_app" >&2
    exit 1
fi

if [[ "$run_tests" -eq 1 ]]; then
    if [[ "$mode" == "debug" ]]; then
        if [[ -n "$qt_prefix" ]]; then
            echo "Running tests from custom Qt build"
            ctest --test-dir "$build_dir" --output-on-failure
        else
            echo "Running tests: macos-debug"
            ctest --preset macos-debug --output-on-failure
        fi
    else
        echo "Release builds do not enable CTest; skipping --test."
    fi
fi

if [[ "$run_rhi" -eq 1 ]]; then
    echo "Running native Metal RHI acceptance tests"
    ctest --preset macos-rhi-acceptance --output-on-failure
fi

if [[ "$command_name" != "package" ]]; then
    echo "Development artifact: $built_app"
    echo "dist/ was not modified."
    exit 0
fi

for tool_name in macdeployqt qtpaths otool install_name_tool codesign ditto file lipo \
                      hdiutil unzip; do
    command -v "$tool_name" >/dev/null 2>&1 || {
        echo "Required packaging tool is missing: $tool_name" >&2
        exit 1
    }
done

stage_dir="$(mktemp -d /tmp/ispview-package.XXXXXX)"
trap 'rm -rf "$stage_dir"' EXIT
staged_app="$stage_dir/MVPImageViewer.app"
ditto "$built_app" "$staged_app"
python3 "$script_dir/scripts/archive_diagnostic_symbols.py" \
    --binary "$built_app/Contents/MacOS/MVPImageViewer" --staged-binary "$staged_app/Contents/MacOS/MVPImageViewer" --output "$script_dir/dist/symbols"
# dSYM belongs to the separate symbol archive, not the application bundle.
if [[ -d "$staged_app/Contents/MacOS/MVPImageViewer.dSYM" ]]; then
    rm -rf "$staged_app/Contents/MacOS/MVPImageViewer.dSYM"
fi

crashpad_enabled="$(sed -n 's/^ISPVIEW_ENABLE_CRASHPAD:BOOL=//p' "$build_dir/CMakeCache.txt")"
if [[ "$crashpad_enabled" == "ON" && ! -x "$staged_app/Contents/Helpers/crashpad_handler" ]]; then
    echo "Crashpad is enabled, but its packaged helper is missing or not executable." >&2
    exit 1
fi

crashpad_root="$(sed -n 's/^ISPVIEW_CRASHPAD_ROOT:PATH=//p' "$build_dir/CMakeCache.txt")"
cmake "-DNOTICE_DESTINATION=$staged_app/Contents/Resources" \
    "-DISPVIEW_CRASHPAD_ROOT=$crashpad_root" \
    "-DISPVIEW_INCLUDE_CRASHPAD_LICENSES=$crashpad_enabled" \
    -P "$script_dir/scripts/package_licenses.cmake"

echo "Deploying Qt and QML dependencies"
deploy_log="$stage_dir/macdeployqt.log"
if ! macdeployqt "$staged_app" \
    -qmldir="$script_dir/src/qml" \
    -always-overwrite \
    -verbose=0 >"$deploy_log" 2>&1; then
    echo "macdeployqt failed; last 80 log lines:" >&2
    tail -n 80 "$deploy_log" >&2
    exit 1
fi

# Homebrew Qt 6.9's macdeployqt can omit frameworks referenced only by QML
# plug-ins. Copy the small, actually used Controls/Dialogs dependency closure.
qt_lib_dir="$(qtpaths --query QT_INSTALL_LIBS)"
required_frameworks=(
    QtDBus.framework
    QtQmlCore.framework
    QtQuickControls2Basic.framework
    QtQuickControls2BasicStyleImpl.framework
    QtQuickControls2Impl.framework
    QtQuickDialogs2.framework
    QtQuickDialogs2QuickImpl.framework
    QtQuickDialogs2Utils.framework
)

for framework_name in "${required_frameworks[@]}"; do
    framework_source="$qt_lib_dir/$framework_name"
    framework_target="$staged_app/Contents/Frameworks/$framework_name"
    if [[ ! -d "$framework_source" ]]; then
        echo "Required Qt framework is missing: $framework_source" >&2
        exit 1
    fi
    if [[ ! -d "$framework_target" ]]; then
        ditto "$framework_source" "$framework_target"
    fi
done

qt_dbus_binary="$staged_app/Contents/Frameworks/QtDBus.framework/Versions/A/QtDBus"
dbus_source="$(otool -L "$qt_dbus_binary" \
    | awk '$1 ~ /libdbus-1/ && $1 ~ /\.dylib/ { print $1; exit }')"
if [[ -n "$dbus_source" && "$dbus_source" == /* ]]; then
    if [[ ! -f "$dbus_source" ]]; then
        echo "QtDBus dependency is missing: $dbus_source" >&2
        exit 1
    fi
    dbus_name="$(basename "$dbus_source")"
    dbus_target="$staged_app/Contents/Frameworks/$dbus_name"
    cp "$dbus_source" "$dbus_target"
    chmod u+w "$dbus_target"
    install_name_tool -change "$dbus_source" \
        "@executable_path/../Frameworks/$dbus_name" "$qt_dbus_binary"
    install_name_tool -id "@executable_path/../Frameworks/$dbus_name" "$dbus_target"
fi

echo "Pruning unused Qt plugins before dependency relocation"
cmake \
    -DPACKAGE_ROOT="$staged_app" \
    -DPACKAGE_PLATFORM=macos \
    -P "$script_dir/scripts/prune_qt_runtime.cmake"

# macdeployqt handles Qt, but third-party dylibs must be copied explicitly.
# Follow the Homebrew dependency graph until every referenced library is staged.
echo "Bundling LibRaw, Exiv2, and their runtime dependencies"
while :; do
    copied_dependency=0
    while IFS= read -r -d '' macho_binary; do
        if ! file -b "$macho_binary" | grep -q 'Mach-O'; then
            continue
        fi
        while IFS= read -r local_dependency; do
            dependency_source=
            case "$local_dependency" in
                /opt/homebrew/*|/usr/local/*)
                    dependency_source="$local_dependency"
                    ;;
                @rpath/*.dylib)
                    dependency_name="${local_dependency##*/}"
                    for candidate in "$brew_prefix/lib/$dependency_name" \
                                     "$brew_prefix"/opt/*/lib/"$dependency_name"; do
                        if [[ -f "$candidate" ]]; then
                            dependency_source="$candidate"
                            break
                        fi
                    done
                    ;;
            esac
            [[ -n "$dependency_source" ]] || continue
            [[ -f "$dependency_source" ]] || {
                echo "Missing dependency: $local_dependency (required by $macho_binary)" >&2
                exit 1
            }
            if [[ "$dependency_source" =~ /([^/]+\.framework)/Versions/[^/]+/ ]]; then
                framework_name="${BASH_REMATCH[1]}"
                framework_root="${dependency_source%%/$framework_name/*}/$framework_name"
                dependency_target="$staged_app/Contents/Frameworks/$framework_name"
                if [[ ! -d "$dependency_target" ]]; then
                    ditto "$framework_root" "$dependency_target"
                    copied_dependency=1
                fi
            elif [[ "$dependency_source" == *.dylib ]]; then
                dependency_target="$staged_app/Contents/Frameworks/$(basename "$dependency_source")"
                if [[ ! -f "$dependency_target" ]]; then
                    cp -L "$dependency_source" "$dependency_target"
                    copied_dependency=1
                fi
            fi
        done < <(otool -L "$macho_binary" | awk 'NR > 1 { print $1 }')
    done < <(find "$staged_app/Contents/MacOS" \
                  "$staged_app/Contents/Frameworks" \
                  "$staged_app/Contents/PlugIns" \
                  "$staged_app/Contents/Resources/qml" \
                  -type f -print0)
    [[ "$copied_dependency" -eq 1 ]] || break
done
for library_pattern in 'libraw*.dylib' 'libexiv2*.dylib'; do
    if [[ -z "$(find "$staged_app/Contents/Frameworks" -maxdepth 1 \
        -name "$library_pattern" -type f -print -quit)" ]]; then
        echo "Required image library was not bundled: $library_pattern" >&2
        exit 1
    fi
done

# macdeployqt cannot rewrite frameworks that were absent during its scan. Relocate every
# package-manager dependency after completing the QML plugin closure. A local dependency that
# was not bundled is a packaging error, rather than something an end-user machine can resolve.
echo "Relocating the completed Qt dependency closure"
macho_candidates=()
while IFS= read -r -d '' macho_candidate; do
    macho_candidates+=("$macho_candidate")
done < <(find "$staged_app/Contents/MacOS" \
              "$staged_app/Contents/Frameworks" \
              "$staged_app/Contents/PlugIns" \
              "$staged_app/Contents/Resources/qml" \
              -type f -print0)

for macho_binary in "${macho_candidates[@]}"; do
    if ! file -b "$macho_binary" | grep -q 'Mach-O'; then
        continue
    fi
    install_id="$(otool -D "$macho_binary" 2>/dev/null | awk 'NR == 2 { print $1 }')"
    while IFS= read -r local_dependency; do
        [[ -n "$local_dependency" ]] || continue
        case "$local_dependency" in
            /opt/homebrew/*|/usr/local/*|@rpath/*.dylib)
                ;;
            *)
                continue
                ;;
        esac
        [[ "$local_dependency" != "$install_id" ]] || continue
        relocated_dependency=
        dependency_target=
        if [[ "$local_dependency" =~ /([^/]+\.framework)/Versions/[^/]+/([^/]+)$ ]]; then
            framework_name="${BASH_REMATCH[1]}"
            framework_binary_name="${BASH_REMATCH[2]}"
            dependency_target="$staged_app/Contents/Frameworks/$framework_name/Versions/A/$framework_binary_name"
            relocated_dependency="@executable_path/../Frameworks/$framework_name/Versions/A/$framework_binary_name"
        elif [[ "$local_dependency" =~ /([^/]+\.dylib)$ ]]; then
            dependency_name="${BASH_REMATCH[1]}"
            dependency_target="$staged_app/Contents/Frameworks/$dependency_name"
            relocated_dependency="@executable_path/../Frameworks/$dependency_name"
        fi
        if [[ -z "$relocated_dependency" || ! -f "$dependency_target" ]]; then
            echo "Local dependency was not bundled: $local_dependency (required by $macho_binary)" >&2
            exit 1
        fi
        install_name_tool -change "$local_dependency" "$relocated_dependency" "$macho_binary"
    done < <(otool -L "$macho_binary" | awk 'NR > 1 { print $1 }')
done

for framework_path in "$staged_app"/Contents/Frameworks/*.framework; do
    [[ -d "$framework_path" ]] || continue
    framework_filename="$(basename "$framework_path")"
    framework_binary_name="${framework_filename%.framework}"
    framework_binary="$framework_path/Versions/A/$framework_binary_name"
    [[ -f "$framework_binary" ]] || continue
    install_name_tool -id \
        "@executable_path/../Frameworks/$framework_filename/Versions/A/$framework_binary_name" \
        "$framework_binary"
done
for dylib_path in "$staged_app"/Contents/Frameworks/*.dylib; do
    [[ -f "$dylib_path" ]] || continue
    install_name_tool -id \
        "@executable_path/../Frameworks/$(basename "$dylib_path")" \
        "$dylib_path"
done

app_binary="$staged_app/Contents/MacOS/MVPImageViewer"
if ! lipo -archs "$app_binary" | tr ' ' '\n' | grep -Fxq arm64; then
    echo "Packaged application does not contain the required arm64 architecture." >&2
    lipo -info "$app_binary" >&2
    exit 1
fi
while IFS= read -r existing_rpath; do
    case "$existing_rpath" in
        /opt/homebrew/*|/usr/local/*)
            install_name_tool -delete_rpath "$existing_rpath" "$app_binary"
            ;;
    esac
done < <(otool -l "$app_binary" | awk '/LC_RPATH/ { getline; getline; print $2 }')

if ! otool -l "$app_binary" | awk '/LC_RPATH/ { getline; getline; print $2 }' \
    | grep -Fxq '@executable_path/../Frameworks'; then
    install_name_tool -add_rpath '@executable_path/../Frameworks' "$app_binary"
fi

# Generate the runtime inventory after relocation and before signing. Missing
# catalog evidence is reported for follow-up, but must not block an installer.
if ! python3 "$script_dir/scripts/collect_runtime_licenses.py" "$staged_app" \
    --output "$staged_app/Contents/Resources"; then
    audit_dir="$script_dir/build/runtime-license-audit-macos"
    mkdir -p "$audit_dir"
    for report in RUNTIME_DEPENDENCIES.json RUNTIME_DEPENDENCIES.md; do
        if [[ -f "$staged_app/Contents/Resources/$report" ]]; then
            cp "$staged_app/Contents/Resources/$report" "$audit_dir/$report"
        fi
    done
    echo "Warning: runtime license audit is incomplete; packaging will continue." >&2
    echo "Inspect $audit_dir and set ISPVIEW_RUNTIME_CATALOG to resolve the findings." >&2
fi

chmod -R u+w "$staged_app"
xattr -cr "$staged_app" 2>/dev/null || true

if [[ "$crashpad_enabled" == "ON" ]]; then
    if [[ "$sign_identity" != "-" ]]; then
        codesign --force --options runtime --timestamp --sign "$sign_identity" "$staged_app/Contents/Helpers/crashpad_handler"
    else
        codesign --force --sign - "$staged_app/Contents/Helpers/crashpad_handler"
    fi
fi
if [[ "$sign_identity" == "-" ]]; then
    echo "Applying ad-hoc local-test signature"
    codesign --force --deep --sign - \
        --entitlements "$script_dir/packaging/macos-local.entitlements" \
        "$staged_app"
else
    echo "Signing with: $sign_identity"
    codesign --force --deep --options runtime --timestamp \
        --sign "$sign_identity" "$staged_app"
fi

codesign --verify --deep --strict --verbose=2 "$staged_app"
local_dependency_report="$stage_dir/local-dependencies.txt"
find "$staged_app/Contents/MacOS" \
     "$staged_app/Contents/Frameworks" \
     "$staged_app/Contents/PlugIns" \
     "$staged_app/Contents/Resources/qml" \
     -type f -print0 \
    | xargs -0 otool -L 2>/dev/null \
    | grep -E '/opt/homebrew|/usr/local' >"$local_dependency_report" || true
if [[ -s "$local_dependency_report" ]]; then
    echo "The package still references local package-manager paths:" >&2
    cat "$local_dependency_report" >&2
    exit 1
fi

version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$staged_app/Contents/Info.plist")"
# The build configuration and architecture check above require arm64 even when
# packaging is run from a cross-compiling Intel host.
architecture="arm64"
dist_dir="$script_dir/dist"
dist_app="$dist_dir/MVPImageViewer.app"
zip_name="MVPImageViewer-${version}-macos-${architecture}.zip"
zip_path="$dist_dir/$zip_name"
dmg_name="MVPImageViewer-${version}-macos-${architecture}.dmg"
dmg_path="$dist_dir/$dmg_name"

mkdir -p "$dist_dir"
rm -rf "$dist_app"
ditto "$staged_app" "$dist_app"

echo "Creating macOS installer image"
dmg_root="$stage_dir/dmg-root"
mkdir -p "$dmg_root"
ditto "$staged_app" "$dmg_root/MVPImageViewer.app"
ln -s /Applications "$dmg_root/Applications"
rm -f "$dmg_path"
hdiutil create \
    -volname "MVP Image Viewer" \
    -srcfolder "$dmg_root" \
    -fs HFS+ \
    -format UDZO \
    -imagekey zlib-level=9 \
    "$dmg_path"
hdiutil verify "$dmg_path"
echo "SHA-256: $(shasum -a 256 "$dmg_path" | awk '{print $1}')"
echo "macOS installer: $dmg_path"

if [[ "$create_zip" -eq 1 ]]; then
    rm -f "$zip_path"
    ditto -c -k --sequesterRsrc --keepParent "$dist_app" "$zip_path"
    unzip -tq "$zip_path"
    echo "SHA-256: $(shasum -a 256 "$zip_path" | awk '{print $1}')"
    echo "Distributable ZIP: $zip_path"
fi

echo "Distributable app: $dist_app"
if [[ "$sign_identity" == "-" ]]; then
    echo "Note: this package is ad-hoc signed. Use --sign with a Developer ID for public distribution."
fi
