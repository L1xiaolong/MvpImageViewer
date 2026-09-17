#!/usr/bin/env bash
set -euo pipefail
# Fixed source revision and its DEPS lock the runtime library and handler together.
revision=4b8fc2b536e04407032cb6eba687cfe68cb5966a
root="${1:?Pass the Crashpad checkout path}"
parent="$(dirname "$root")"
if [[ "$(basename "$root")" != "crashpad" ]]; then
    echo "Crashpad DEPS require a checkout directory named crashpad" >&2
    exit 1
fi
mkdir -p "$parent"
if ! command -v gclient >/dev/null; then
    tools="$parent/depot_tools"
    if [[ ! -d "$tools/.git" ]]; then
        git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$tools"
    fi
    export PATH="$tools:$PATH"
fi
cd "$parent"
if [[ ! -f .gclient ]]; then
    gclient config --name="$(basename "$root")" https://chromium.googlesource.com/crashpad/crashpad.git
fi
gclient sync --revision "$(basename "$root")@$revision" --no-history
cd "$root"
# Build the client separately so its complete object closure excludes the handler's main().
gn gen out/ispview --args='is_debug=false target_cpu="arm64"'
ninja -C out/ispview client
python3 - <<'PY'
from pathlib import Path
import subprocess
objects = sorted(Path('out/ispview/obj').rglob('*.o'))
if not objects:
    raise SystemExit('No Crashpad client objects produced')
subprocess.run(['xcrun', 'libtool', '-static', '-o', 'out/ispview/libispview_crashpad.a',
                *map(str, objects)], check=True)
PY
gn gen out/ispview-handler --args='is_debug=false target_cpu="arm64"'
ninja -C out/ispview-handler crashpad_handler
printf '%s\n' "$revision" > out/ispview/ispview-revision
