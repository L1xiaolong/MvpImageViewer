#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_release=false

if [[ "${1:-}" == "--release" ]]; then
    build_release=true
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--release]" >&2
    exit 64
fi

cd "$project_dir"

echo "[1/6] Configure macOS Debug"
cmake --preset macos-debug

echo "[2/6] Build production QML app and tests"
cmake --build --preset macos-debug --parallel

echo "[3/6] Run QML static analysis"
cmake --build --preset macos-debug --target ISPImageViewerQml_qmllint --parallel

echo "[4/6] Run all automated tests"
ctest --test-dir build/macos-preset-debug --output-on-failure

echo "[5/6] Audit production UI dependencies"
if rg -n 'Qt6::Widgets|#include <Q(Application|Widget|MainWindow|Dialog)>' src CMakeLists.txt; then
    echo "Production source contains a forbidden Qt Widgets dependency." >&2
    exit 1
fi

debug_executable="build/macos-preset-debug/src/qml/MVPImageViewer.app/Contents/MacOS/MVPImageViewer"
test -x "$debug_executable"
if otool -L "$debug_executable" | rg -q 'QtWidgets'; then
    echo "Production executable links QtWidgets." >&2
    exit 1
fi

echo "[6/6] Verify optional Release build"
if $build_release; then
    cmake --preset macos-release
    cmake --build --preset macos-release --parallel
    release_executable="build/macos-preset-release/src/qml/MVPImageViewer.app/Contents/MacOS/MVPImageViewer"
    test -x "$release_executable"
    if otool -L "$release_executable" | rg -q 'QtWidgets'; then
        echo "Release executable links QtWidgets." >&2
        exit 1
    fi
else
    echo "Skipped (run with --release before packaging)."
fi

echo "QML regression automation passed."
