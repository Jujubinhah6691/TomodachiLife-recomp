#!/bin/sh
# Build the launcher (launcher\launcher.cpp) into build\work\TomodachiLauncher.exe, beside the game and SDL3.dll.
#   sh scripts/build_launcher.sh
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
D=${SSBU_DEPS:-$REPO/../ssbu-recomp/deps}
CLANG="$D/llvm/bin/clang.exe"
IMGUI="$D/imgui"
SDL="$D/SDL3"
OUT="$REPO/build/work/TomodachiLauncher.exe"
OBJ="$REPO/build/work/obj_launcher"
mkdir -p "$OBJ"
VER=$(git -C "$REPO" describe --always --dirty 2>/dev/null || echo dev)
CXX="-x c++ -std=c++14 -O2 -w -fno-exceptions -fno-rtti -D_CRT_SECURE_NO_WARNINGS -I$(cygpath -w "$IMGUI") -I$(cygpath -w "$IMGUI/backends") -I$(cygpath -w "$SDL/include")"
objs=""
for src in "$REPO/launcher/launcher.cpp" "$IMGUI/imgui.cpp" "$IMGUI/imgui_draw.cpp" "$IMGUI/imgui_tables.cpp" "$IMGUI/imgui_widgets.cpp" \
           "$IMGUI/backends/imgui_impl_sdl3.cpp" "$IMGUI/backends/imgui_impl_sdlrenderer3.cpp"; do
    o="$OBJ/$(basename "$src" .cpp).o"
    if [ ! -f "$o" ] || [ "$src" -nt "$o" ]; then
        "$CLANG" $CXX -DLAUNCHER_VERSION="\"$VER\"" -c "$(cygpath -w "$src")" -o "$(cygpath -w "$o")"
    fi
    objs="$objs $(cygpath -w "$o")"
done
"$CLANG" $objs "$(cygpath -w "$SDL/lib/x64/SDL3.lib")" -luser32 -lshell32 -lole32 -lgdi32 -ladvapi32 \
    -Wl,/SUBSYSTEM:WINDOWS -Wl,/ENTRY:mainCRTStartup -o "$(cygpath -w "$OUT")"
[ -f "$REPO/build/work/SDL3.dll" ] || cp "$SDL/lib/x64/SDL3.dll" "$REPO/build/work/"
echo "built $OUT"
