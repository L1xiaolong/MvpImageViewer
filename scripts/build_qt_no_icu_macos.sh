#!/usr/bin/env bash
set -euo pipefail

qt_version="6.9.0"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 6)"
clean=0

print_usage() {
    cat <<'EOF'
Usage: ./scripts/build_qt_no_icu_macos.sh [--clean] [-j N]

Builds the Qt modules used by MVP Image Viewer without ICU. The installation is
written to build/qt-no-icu/install and can be selected with:

  ./build_macos.sh package --qt-prefix build/qt-no-icu/install

Options:
  --clean  Recreate the complete custom Qt build.
  -j N     Parallel build jobs.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            clean=1
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

[[ "$(uname -s)" == "Darwin" ]] || {
    echo "This script builds the macOS Qt variant only." >&2
    exit 2
}

for tool_name in cmake curl make patch perl shasum tar; do
    command -v "$tool_name" >/dev/null 2>&1 || {
        echo "Required build tool is missing: $tool_name" >&2
        exit 1
    }
done

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
qt_root="$repo_dir/build/qt-no-icu"
download_dir="$qt_root/downloads"
source_dir="$qt_root/src"
install_dir="$qt_root/install"
base_url="https://download.qt.io/official_releases/qt/6.9/${qt_version}/submodules"

if [[ "$clean" -eq 1 ]]; then
    echo "Removing custom Qt build: $qt_root"
    rm -rf "$qt_root"
fi
mkdir -p "$download_dir" "$source_dir" "$install_dir"

fetch_module() {
    module_name="$1"
    expected_sha="$2"
    archive="$download_dir/${module_name}.tar.xz"
    source_name="${module_name}-everywhere-src-${qt_version}"
    url="$base_url/${source_name}.tar.xz"

    if [[ -f "$archive" ]] &&
       [[ "$(shasum -a 256 "$archive" | awk '{print $1}')" != "$expected_sha" ]]; then
        echo "Discarding an invalid download: $archive"
        rm -f "$archive"
    fi
    if [[ ! -f "$archive" ]]; then
        echo "Downloading $module_name $qt_version"
        curl --fail --location --retry 3 --output "$archive" "$url"
    fi
    actual_sha="$(shasum -a 256 "$archive" | awk '{print $1}')"
    [[ "$actual_sha" == "$expected_sha" ]] || {
        echo "SHA-256 mismatch for $archive" >&2
        exit 1
    }
    if [[ ! -d "$source_dir/$source_name" ]]; then
        echo "Extracting $module_name"
        tar -xJf "$archive" -C "$source_dir"
    fi
}

fetch_module qtbase c1800c2ea835801af04a05d4a32321d79a93954ee3ae2172bbeacf13d1f0598c
fetch_module qtshadertools 916c40281ac3dee23b163f6ca73fb5bdeee344838b9a922b6f36269642d6f4bb
fetch_module qtdeclarative a3175fa510847a136734f989e2bfea7f7bbb9dc9acc98b40b544d26f5ba20d3d
fetch_module qtsvg ec359d930c95935ea48af58b100c2f5d0d275968ec8ca1e0e76629b7159215fc
fetch_module qttools fa645589cc3f939022401a926825972a44277dead8ec8607d9f2662e6529c9a4

qtbase_source="$source_dir/qtbase-everywhere-src-${qt_version}"
agl_probe="$qtbase_source/cmake/FindWrapOpenGL.cmake"
if ! grep -Fq 'WrapOpenGL_AGL AND EXISTS' "$agl_probe"; then
    echo "Patching Qt 6.9 for SDKs where the obsolete AGL binary is absent"
    patch -d "$qtbase_source" -p1 <"$repo_dir/packaging/qt-6.9-macos-agl.patch"
fi

# Qt 6.9 generated helper scripts do not quote $0. A workspace path containing
# spaces therefore breaks module configuration unless the templates are fixed.
while IFS= read -r helper_template; do
    if grep -Fq 'dirname $0' "$helper_template"; then
        perl -pi -e 's/dirname \$0/dirname "\$0"/g' "$helper_template"
    fi
done < <(grep -rl 'dirname \$0' "$qtbase_source/bin" "$qtbase_source/libexec")

qtbase_build="$qt_root/build-qtbase"
qtcore_binary="$install_dir/lib/QtCore.framework/Versions/A/QtCore"
qtcore_links_icu() {
    otool -L "$1" | awk 'NR > 1 { print $1 }' | grep -Eqi '/libicu[^/]*\.(dylib|so)'
}
if [[ -f "$qtcore_binary" ]] && ! qtcore_links_icu "$qtcore_binary"; then
    touch "$qt_root/.qtbase-installed"
fi
if [[ ! -f "$qt_root/.qtbase-installed" ]]; then
    if [[ ! -f "$qtbase_build/CMakeCache.txt" ]]; then
        mkdir -p "$qtbase_build"
        echo "Configuring QtBase without ICU"
        (
            cd "$qtbase_build"
            "$qtbase_source/configure" \
                -prefix "$install_dir" \
                -release -optimize-size -shared -framework -no-icu \
                -nomake examples -nomake tests \
                -qt-zlib -qt-libjpeg -qt-libpng -qt-pcre \
                -qt-harfbuzz -qt-freetype \
                -- -DCMAKE_OSX_ARCHITECTURES=arm64
        )
    fi
    cmake --build "$qtbase_build" --parallel "$jobs"
    cmake --install "$qtbase_build"
    if qtcore_links_icu "$qtcore_binary"; then
        echo "Verification failed: the custom QtCore still links ICU." >&2
        exit 1
    fi
    touch "$qt_root/.qtbase-installed"
fi

configure_build_install_module() {
    module_name="$1"
    verification_path="$2"
    module_source="$source_dir/${module_name}-everywhere-src-${qt_version}"
    module_build="$qt_root/build-${module_name}"
    module_stamp="$qt_root/.${module_name}-installed"
    if [[ -e "$verification_path" ]]; then
        touch "$module_stamp"
    fi
    if [[ -f "$module_stamp" ]]; then
        return
    fi
    if [[ ! -f "$module_build/CMakeCache.txt" ]]; then
        mkdir -p "$module_build"
        (
            cd "$module_build"
            "$install_dir/bin/qt-configure-module" "$module_source" -- \
                -DCMAKE_BUILD_TYPE=Release \
                -DQT_BUILD_EXAMPLES=OFF \
                -DQT_BUILD_TESTS=OFF \
                -DQT_BUILD_EXAMPLES_BY_DEFAULT=OFF \
                -DQT_BUILD_TESTS_BY_DEFAULT=OFF
        )
    fi
    cmake --build "$module_build" --parallel "$jobs"
    cmake --install "$module_build"
    [[ -e "$verification_path" ]] || {
        echo "Module installation is incomplete: $verification_path" >&2
        exit 1
    }
    touch "$module_stamp"
}

configure_build_install_module \
    qtshadertools "$install_dir/lib/QtShaderTools.framework/Versions/A/QtShaderTools"
configure_build_install_module \
    qtdeclarative "$install_dir/lib/QtQuickDialogs2.framework/Versions/A/QtQuickDialogs2"
configure_build_install_module \
    qtsvg "$install_dir/lib/QtSvg.framework/Versions/A/QtSvg"
configure_build_install_module \
    qttools "$install_dir/bin/lrelease"

if qtcore_links_icu "$qtcore_binary"; then
    echo "Verification failed: the custom QtCore still links ICU." >&2
    exit 1
fi

echo "No-ICU Qt installation: $install_dir"
echo "Build the package with:"
echo "  ./build_macos.sh package --qt-prefix \"$install_dir\""
