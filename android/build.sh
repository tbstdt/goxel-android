#!/bin/bash
# Build the goxel Android APK without gradle, using only the SDK/NDK tools.
#
# Env:
#   ANDROID_HOME   SDK location (default ~/android/sdk)
#   JAVA_HOME      JDK 17      (default ~/android/jdk)
#   ABIS           ABIs to build (default "arm64-v8a armeabi-v7a")
#   MODE           release or debug (default release)
#
# Output: android/build/goxel.apk

set -euo pipefail

cd "$(dirname "$0")"
HERE=$PWD

ANDROID_HOME=${ANDROID_HOME:-$HOME/android/sdk}
JAVA_HOME=${JAVA_HOME:-$HOME/android/jdk}
ABIS=${ABIS:-"arm64-v8a armeabi-v7a"}
MODE=${MODE:-release}
API=34
MIN_API=24
NDK=$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)
BT=$(ls -d "$ANDROID_HOME"/build-tools/* | sort -V | tail -1)
CMAKE=$(ls -d "$ANDROID_HOME"/cmake/*/bin | sort -V | tail -1)
PLATFORM=$ANDROID_HOME/platforms/android-$API/android.jar
OUT=$HERE/build
export PATH=$JAVA_HOME/bin:$CMAKE:$PATH

if [ "$MODE" = debug ]; then BUILD_TYPE=Debug; else BUILD_TYPE=Release; fi

mkdir -p "$OUT"
rm -rf "$OUT/apk" && mkdir -p "$OUT/apk/lib"

# 1. Native library, one per ABI.
for abi in $ABIS; do
    echo "== native $abi"
    cmake -S jni -B "$OUT/cmake-$abi" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DCMAKE_MAKE_PROGRAM="$CMAKE/ninja" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-$MIN_API \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE > /dev/null
    cmake --build "$OUT/cmake-$abi" -j"$(nproc)"
    mkdir -p "$OUT/apk/lib/$abi"
    cp "$OUT/cmake-$abi/libgoxel.so" "$OUT/apk/lib/$abi/"
    if [ "$MODE" != debug ]; then
        "$NDK"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip \
            "$OUT/apk/lib/$abi/libgoxel.so"
    fi
done

# 2. Resources and manifest.
echo "== resources"
rm -rf "$OUT/res.zip" "$OUT/gen" && mkdir -p "$OUT/gen"
"$BT/aapt2" compile --dir res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM" \
    --manifest AndroidManifest.xml --java "$OUT/gen" \
    --min-sdk-version $MIN_API --target-sdk-version $API \
    "$OUT/res.zip"

# 3. Java code.
echo "== java"
rm -rf "$OUT/classes" && mkdir -p "$OUT/classes"
javac -source 8 -target 8 -nowarn -Xlint:-options \
    -classpath "$PLATFORM" -d "$OUT/classes" \
    $(find java "$OUT/gen" -name '*.java')
"$BT/d8" --min-api $MIN_API --lib "$PLATFORM" --output "$OUT/apk" \
    $(find "$OUT/classes" -name '*.class')

# 4. Package, align, sign.
echo "== package"
cp "$OUT/base.apk" "$OUT/unsigned.apk"
if command -v zip > /dev/null; then
    (cd "$OUT/apk" && zip -qr "$OUT/unsigned.apk" classes.dex lib)
else
    (cd "$OUT/apk" && python3 -c '
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "a", zipfile.ZIP_DEFLATED) as z:
    z.write("classes.dex")
    for root, dirs, files in os.walk("lib"):
        for f in sorted(files): z.write(os.path.join(root, f))
' "$OUT/unsigned.apk")
fi
"$BT/zipalign" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

KEYSTORE=${KEYSTORE:-$HOME/.android/goxel-debug.keystore}
if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkeypair -keystore "$KEYSTORE" -storepass android \
        -keypass android -alias goxel -keyalg RSA -keysize 2048 \
        -validity 10000 -dname "CN=Goxel Android Debug" > /dev/null
fi
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android \
    --key-pass pass:android --out "$OUT/goxel.apk" "$OUT/aligned.apk"
rm -f "$OUT/unsigned.apk" "$OUT/aligned.apk"
echo "== done: $OUT/goxel.apk ($(du -h "$OUT/goxel.apk" | cut -f1))"
