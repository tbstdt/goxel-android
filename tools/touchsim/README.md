# Touch UI simulator

Runs goxel with the touch UI (`GOXEL_TOUCH_UI`, same GLES code path as the
android port) in an offscreen EGL surface, driven by a script of touch
events, and saves screenshots.  Used to check the touch UI without a
device.

Build (zig is used as the C/C++ compiler, the EGL/GLES headers come from the
android NDK):

    GL_LIBS=/usr/lib/x86_64-linux-gnu tools/touchsim/build.sh

Run (needs Mesa with the surfaceless EGL platform, e.g. llvmpipe):

    tools/touchsim/build/goxel-touchsim script.txt

See the comment at the top of `main_sim.c` for the script commands, e.g.:

    size 1080 2400 2.625
    tap 24 110
    shot palette.png
