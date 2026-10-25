#!/bin/sh
# Build the launcher (launcher\launcher.cpp) into build\work\TomodachiLauncher.exe, beside the game and SDL3.dll.
#   sh scripts/build_launcher.sh
# Its pictures (title-logo.png, launcher-background.jpg) come from the player's game files: scripts/launcher_art.py.
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
D=${SSBU_DEPS:-$REPO/../ssbu-recomp/deps}
# A clang with a C++17 standard library that the installed Visual Studio accepts (it asks for clang 20 or newer; the
# port's own clang 9 compiles only C): LAUNCHER_CLANG, else the LLVM installer's, else the port's.
CLANG=${LAUNCHER_CLANG:-"/c/Program Files/LLVM/bin/clang.exe"}
[ -x "$CLANG" ] || CLANG="$D/llvm/bin/clang.exe"
SDL="$D/SDL3"
OUT="$REPO/build/work/TomodachiLauncher.exe"
OBJ="$REPO/build/work/obj_launcher"
mkdir -p "$OBJ"
VER="0.1.0 ($(git -C "$REPO" describe --always --dirty 2>/dev/null || echo dev))"
o="$OBJ/launcher.o"
"$CLANG" -x c++ -std=c++17 -O2 -w -D_CRT_SECURE_NO_WARNINGS -DLAUNCHER_VERSION="\"$VER\"" -I"$(cygpath -w "$REPO/launcher")" -I"$(cygpath -w "$REPO/third_party/stb")" \
    -I"$(cygpath -w "$SDL/include")" -c "$(cygpath -w "$REPO/launcher/launcher.cpp")" -o "$(cygpath -w "$o")"
"$CLANG" "$(cygpath -w "$o")" "$(cygpath -w "$SDL/lib/x64/SDL3.lib")" -luser32 -lshell32 -lole32 -lgdi32 -ladvapi32 \
    -Wl,/SUBSYSTEM:WINDOWS -Wl,/ENTRY:mainCRTStartup -o "$(cygpath -w "$OUT")"
[ -f "$REPO/build/work/SDL3.dll" ] || cp "$SDL/lib/x64/SDL3.dll" "$REPO/build/work/"
echo "built $OUT"
