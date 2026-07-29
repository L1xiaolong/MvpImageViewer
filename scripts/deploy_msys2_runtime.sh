#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./scripts/deploy_msys2_runtime.sh PACKAGE_DIRECTORY

Copies every UCRT64 runtime DLL imported by the packaged Windows executables,
Qt libraries, plugins, and QML modules. Dependencies are resolved recursively
and the finished package is audited before this script exits successfully.

Optional environment variables:
  UCRT64_BIN_DIR  MSYS2 UCRT64 binary directory (default: /ucrt64/bin)
  OBJDUMP_BIN     objdump executable (default: UCRT64_BIN_DIR/objdump.exe)
EOF
}

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 2
fi

package_directory="$1"
runtime_directory="${UCRT64_BIN_DIR:-/ucrt64/bin}"
objdump_binary="${OBJDUMP_BIN:-$runtime_directory/objdump.exe}"

if [[ ! -d "$package_directory" ]]; then
    echo "Package directory does not exist: $package_directory" >&2
    exit 2
fi
if [[ ! -f "$package_directory/MVPImageViewer.exe" ]]; then
    echo "MVPImageViewer.exe is missing from: $package_directory" >&2
    exit 2
fi
if [[ ! -d "$runtime_directory" ]]; then
    echo "UCRT64 runtime directory does not exist: $runtime_directory" >&2
    exit 2
fi
if [[ ! -x "$objdump_binary" ]]; then
    echo "objdump is not executable: $objdump_binary" >&2
    exit 2
fi

package_directory="$(cd -- "$package_directory" && pwd)"
runtime_directory="$(cd -- "$runtime_directory" && pwd)"

imported_dlls() {
    "$objdump_binary" -p "$1" 2>/dev/null |
        sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' |
        sed '/^[[:space:]]*$/d'
}

declare -A packaged_dlls=()
while IFS= read -r -d '' packaged_dll; do
    packaged_name="$(basename -- "$packaged_dll")"
    packaged_dlls["${packaged_name,,}"]="$packaged_dll"
done < <(find "$package_directory" -maxdepth 1 -type f -iname '*.dll' -print0)

declare -A runtime_dlls=()
while IFS= read -r -d '' runtime_dll; do
    runtime_name="$(basename -- "$runtime_dll")"
    runtime_dlls["${runtime_name,,}"]="$runtime_dll"
done < <(find "$runtime_directory" -maxdepth 1 -type f -iname '*.dll' -print0)

copied_total=0
round=0
while true; do
    round=$((round + 1))
    copied_this_round=0

    while IFS= read -r -d '' portable_executable; do
        while IFS= read -r dependency; do
            [[ -n "$dependency" ]] || continue
            dependency_key="${dependency,,}"
            [[ -z "${packaged_dlls[$dependency_key]:-}" ]] || continue

            runtime_dll="${runtime_dlls[$dependency_key]:-}"
            [[ -n "$runtime_dll" ]] || continue

            destination="$package_directory/$(basename -- "$runtime_dll")"
            cp -f -- "$runtime_dll" "$destination"
            packaged_dlls[$dependency_key]="$destination"
            echo "Bundled runtime dependency: $(basename -- "$runtime_dll")"
            copied_this_round=$((copied_this_round + 1))
        done < <(imported_dlls "$portable_executable")
    done < <(find "$package_directory" -type f \
        \( -iname '*.exe' -o -iname '*.dll' \) -print0)

    copied_total=$((copied_total + copied_this_round))
    if (( copied_this_round == 0 )); then
        break
    fi
done

missing_count=0
while IFS= read -r -d '' portable_executable; do
    while IFS= read -r dependency; do
        [[ -n "$dependency" ]] || continue
        dependency_key="${dependency,,}"
        runtime_dll="${runtime_dlls[$dependency_key]:-}"
        [[ -n "$runtime_dll" ]] || continue

        if [[ -z "${packaged_dlls[$dependency_key]:-}" ]]; then
            echo "Missing runtime dependency: $dependency (required by $portable_executable)" >&2
            missing_count=$((missing_count + 1))
        fi
    done < <(imported_dlls "$portable_executable")
done < <(find "$package_directory" -type f \
    \( -iname '*.exe' -o -iname '*.dll' \) -print0)

if (( missing_count != 0 )); then
    echo "Windows package audit failed with $missing_count missing UCRT64 dependencies." >&2
    exit 1
fi

echo "Windows runtime audit passed; copied $copied_total DLLs in $round scan rounds."
