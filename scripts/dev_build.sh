#!/bin/sh
# Stage and rebuild the host exe (and the game DLL with "dll") without running the game.
#   sh scripts/dev_build.sh [dll]
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
D=${SSBU_DEPS:-$REPO/../ssbu-recomp/deps}
PY=$D/python/python.exe
"$PY" "$REPO/scripts/stage.py" > /dev/null
cd "$REPO/build/work"
export RECOMP_CLANG=$(cygpath -w "$D/llvm/bin/clang.exe") SDL3_ROOT=$(cygpath -w "$D/SDL3") VULKAN_HEADERS=$(cygpath -w "$D/Vulkan-Headers")
export IMGUI_ROOT=$(cygpath -w "$D/imgui") FFMPEG_ROOT=$(cygpath -w "$D/ffmpeg")
[ "$1" = dll ] && "$PY" build.py gamedll | tail -1
"$PY" build.py host > host_build.log 2>&1 || { grep -n "error" host_build.log | head -20; exit 1; }
tail -1 host_build.log
