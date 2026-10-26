Goxel for Android (unofficial port)
===================================

This is an unofficial Android port of [Goxel](https://github.com/guillaumechereau/goxel),
the open source voxel editor by Guillaume Chereau.

It is built from the desktop GPL-3 code: GLFW is replaced by a
NativeActivity + EGL backend, file dialogs use the system file picker, and
the camera is controlled with touch gestures. It is **not** the official
Goxel app from Google Play (that one is built from a separate, closed
mobile branch) and is not affiliated with the Goxel author.

Status: early work in progress. The editor runs, but the interface is still
the desktop one and is not adapted to phone screens yet.

Development happens on the `dev` branch.


Touch controls
--------------

- One finger on a voxel: use the current tool.
- One finger on empty space: rotate the view.
- Two fingers: drag to rotate, pinch to zoom.
- Two fingers tap: undo.
- View cube (top right): drag to rotate, tap a face to look at it.
- Round button below the view cube: drag it to pan the view.
- The menu is behind the hamburger button of the top bar.
- Menus and panels scroll with a swipe.

Three fingers gestures are not used: Android reserves them (screenshot).


Building the APK
----------------

No gradle: [`android/build.sh`](android/build.sh) builds the APK directly
with the Android SDK tools.

Requirements: JDK 17, Android SDK with `platforms;android-34`,
`build-tools;34.0.0`, `ndk;26.3.11579264`, `cmake;3.22.1`, and `zip`.

    ANDROID_HOME=~/android/sdk JAVA_HOME=~/android/jdk android/build.sh

Output: `android/build/goxel.apk` (arm64-v8a + armeabi-v7a, signed with a
local debug key). See [android/README.md](android/README.md) for details on
the port layout.


Licence
-------

Like upstream Goxel, this port is released under the GNU GPL3 licence.
Changes in this fork are not covered by the upstream CLA.



Original Goxel README
====================

Version 0.15.1

By Guillaume Chereau <guillaume@noctua-software.com>

[![Build Status](https://github.com/guillaumechereau/goxel/actions/workflows/ci.yml/badge.svg)](https://github.com/guillaumechereau/goxel/actions/workflows/ci.yml)
[![DebianBadge](https://badges.debian.net/badges/debian/unstable/goxel/version.svg)](https://packages.debian.org/unstable/goxel)

Official webpage: https://goxel.xyz

About
-----

You can use goxel to create voxel graphics (3D images formed of cubes).  It
works on Linux, BSD, Windows and macOS.


Download
--------

The last release files can be downloaded from [there](
https://github.com/guillaumechereau/goxel/releases/latest).

Goxel is also available for [iOS](
https://itunes.apple.com/us/app/goxel-3d-voxel-editor/id1259097826) and
[Android](
https://play.google.com/store/apps/details?id=com.noctuasoftware.goxel).


![goxel screenshot 0](https://goxel.xyz/gallery/thibault-fisherman-house.jpg)
Fisherman house, made with Goxel by
[Thibault Simar](https://www.artstation.com/exm)


Licence
-------

Goxel is released under the GNU GPL3 licence.  If you want to use the code
with a commercial project please contact me: I am willing to provide a
version of the code under a commercial license.


Features
--------

- 24 bits RGB colors.
- Unlimited scene size.
- Unlimited undo buffer.
- Layers.
- Marching Cube rendering.
- Procedural rendering.
- Export to obj, pyl, png, magica voxel, qubicle.
- Ray tracing.


Usage
-----

- Left click: apply selected tool operation.
- Middle click: rotate the view.
- right click: pan the view.
- Left/Right arrow: rotate the view.
- Mouse wheel: zoom in and out.


Building
--------

The building system uses scons.  You can compile in debug with 'scons', and in
release with 'scons mode=release'.  On Windows, currently possible to build
with [msys2](https://www.msys2.org/) or try prebuilt
[goxel](https://packages.msys2.org/base/mingw-w64-goxel) package directly.
The code is in C99, using some gnu extensions, so it does not compile
with msvc.

# Linux/BSD

Install dependencies using your package manager.  On Debian/Ubuntu:

    - scons
    - pkg-config
    - libglfw3-dev
    - libgtk-3-dev

Then to build, run the command:

    make release

# Windows

You need to install msys2 mingw, and the following packages:

    pacman -S mingw-w64-x86_64-gcc
    pacman -S mingw-w64-x86_64-glfw
    pacman -S mingw-w64-x86_64-libtre-git
    pacman -S scons
    pacman -S make

Then to build:

    make release


Contributing
------------

In order for your contribution to Goxel to be accepted, you have to sign the
[Goxel Contributor License Agreement (CLA)](doc/cla/sign-cla.md).  This is
mostly to allow me to distribute the mobile branch goxel under a non GPL
licence.

Also, please read the [contributing document](CONTRIBUTING.md).
