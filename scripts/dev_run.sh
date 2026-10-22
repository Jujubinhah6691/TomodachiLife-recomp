#!/bin/sh
# Stage, rebuild what changed (exe; game DLL with "dll"), run the game for N seconds with its log in build/work/<log>.
#   sh scripts/dev_run.sh <log name> [seconds] [dll]
# Tools come from the ssbu-recomp checkout beside this one (its deps folder).
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
D=${SSBU_DEPS:-$REPO/../ssbu-recomp/deps}
PY=$D/python/python.exe
"$PY" "$REPO/scripts/stage.py" > /dev/null
cd "$REPO/build/work"
export RECOMP_CLANG=$(cygpath -w "$D/llvm/bin/clang.exe") SDL3_ROOT=$(cygpath -w "$D/SDL3") VULKAN_HEADERS=$(cygpath -w "$D/Vulkan-Headers")
export IMGUI_ROOT=$(cygpath -w "$D/imgui") FFMPEG_ROOT=$(cygpath -w "$D/ffmpeg")
[ "$3" = dll ] && "$PY" build.py gamedll | tail -1
"$PY" build.py host > host_build.log 2>&1 || { tail -20 host_build.log; exit 1; }
tail -1 host_build.log
export SSBU_ROMFS=$(cygpath -w ../dump/romfs) SSBU_ERROR_DIALOG=0 SSBU_RENDERER=${SSBU_RENDERER:-gl}   # the Vulkan back end lacks counters (nvnCommandBufferReportCounter) so far
set +e
timeout $(( ${2:-40} + 60 )) ./ssbu_native.exe "$(cygpath -w ../dump/exefs/main.elf)" "${2:-40}" > "$1" 2>&1
echo "exit $?"
grep -v "is not an import of the game\|is in sdklift_install.h\|was not lifted" "$1" | grep -n "^\[phase\|HOST EXCEPTION\|^GA:\|without a handler\|guest_trap\|TRAP\|stopping\|frames presented\|still running" | head -40
