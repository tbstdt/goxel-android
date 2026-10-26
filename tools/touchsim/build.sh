#!/bin/bash
# Build the touch UI simulator (see main_sim.c) for linux x86_64.
#
# Needs: zig (used as C/C++ compiler), the Khronos EGL/GLES headers (taken
# from the android NDK), and Mesa libEGL / libGLESv2 at runtime.
#
# Environment:
#   ZIG       zig executable (default: zig in PATH).
#   NDK       android NDK, for the EGL/GLES headers.
#   GL_LIBS   directory with libEGL.so.1 and libGLESv2.so.2.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$HERE/build
ZIG=${ZIG:-zig}
NDK=${NDK:-$HOME/android/sdk/ndk/26.3.11579264}
GL_LIBS=${GL_LIBS:-/usr/lib/x86_64-linux-gnu}
TARGET=x86_64-linux-gnu.2.31
JOBS=${JOBS:-$(nproc)}

mkdir -p "$OUT/obj" "$OUT/include"

# Only the Khronos headers from the NDK, not the android ones.
NDK_INC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include
for d in EGL GLES2 GLES3 KHR; do
    [ -d "$OUT/include/$d" ] || cp -r "$NDK_INC/$d" "$OUT/include/"
done

FLAGS="-target $TARGET -O1 -g -DGLES2=1 -DGOXEL_TOUCH_UI=1 -DUSE_OZONE
    -include $ROOT/src/config.h
    -I$ROOT/src -I$ROOT -I$ROOT/ext_src -I$ROOT/ext_src/uthash
    -I$ROOT/ext_src/stb -I$ROOT/ext_src/xxhash -I$ROOT/ext_src/meshoptimizer
    -I$OUT/include
    -Wall -Wno-unknown-pragmas -Wno-unknown-warning-option
    -fno-sanitize=undefined"

compile() {
    local src=$1
    local obj=$OUT/obj/$(echo "${src#$ROOT/}" | tr / _).o
    if [ "$obj" -nt "$src" ] && [ -z "$FORCE" ]; then echo "$obj"; return; fi
    case "$src" in
    *.c)   $ZIG cc  $FLAGS -std=gnu99 -c "$src" -o "$obj" >&2 ;;
    *.cpp) $ZIG c++ $FLAGS -std=gnu++17 -Wno-narrowing -c "$src" -o "$obj" >&2 ;;
    esac
    echo "$obj"
}
export -f compile
export ZIG FLAGS OUT ROOT FORCE

SOURCES=$(find "$ROOT/src" -name '*.c' -o -name '*.cpp' | grep -v '/src/main.c$')
SOURCES="$SOURCES $HERE/main_sim.c"

echo "== compile ($JOBS jobs)"
OBJS=$(printf '%s\n' $SOURCES | xargs -P "$JOBS" -I{} bash -c 'compile {}')

echo "== link"
$ZIG c++ -target $TARGET $OBJS \
    "$GL_LIBS/libEGL.so.1" "$GL_LIBS/libGLESv2.so.2" \
    -lm -lpthread -ldl -o "$OUT/goxel-touchsim"
echo "== done: $OUT/goxel-touchsim"
