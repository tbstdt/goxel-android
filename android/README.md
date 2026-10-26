# Goxel for Android

Android port of the desktop Goxel code (GPL-3).

- `jni/main_android.c` — replaces `src/main.c`: NativeActivity + EGL (GLES3
  context, goxel built with its `GLES2` code path), touch/mouse/keyboard input.
- `java/.../GoxelActivity.java` — file open/save through the system file
  picker (Storage Access Framework), soft keyboard.
- No gradle: `build.sh` builds the APK directly with the SDK tools.

## Build

Requirements: JDK 17, Android SDK with `platforms;android-34`,
`build-tools;34.0.0`, `ndk;26.3.11579264`, `cmake;3.22.1`, and `zip`.

    ANDROID_HOME=~/android/sdk JAVA_HOME=~/android/jdk ./build.sh

Output: `build/goxel.apk` (arm64-v8a + armeabi-v7a, signed with a local
debug key).

## Touch controls

- One finger on a voxel: use the current tool.
- One finger on empty space: rotate the view.
- Two fingers: drag to rotate, pinch to zoom.
- Two fingers tap: undo.
- View cube (top right): drag to rotate, tap a face to look at it.
- Small round button at the bottom center: drag it to pan the view (can be
  hidden in Edit > Settings > Inputs).
- The menu is behind the hamburger button of the top bar.
- Menus and panels scroll with a swipe.

Three fingers gestures are not used: Android reserves them (screenshot).
