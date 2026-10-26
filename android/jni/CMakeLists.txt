cmake_minimum_required(VERSION 3.18)
project(goxel C CXX)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Same sources as the SConstruct: everything in src/, except the GLFW main.
file(GLOB_RECURSE SOURCES ${ROOT}/src/*.c ${ROOT}/src/*.cpp)
list(REMOVE_ITEM SOURCES ${ROOT}/src/main.c)

add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(goxel SHARED ${SOURCES} main_android.c)

target_include_directories(goxel PRIVATE
    ${ROOT}/src ${ROOT}
    ${ROOT}/ext_src ${ROOT}/ext_src/uthash ${ROOT}/ext_src/stb
    ${ROOT}/ext_src/xxhash ${ROOT}/ext_src/meshoptimizer)

target_compile_definitions(goxel PRIVATE GLES2=1 GOXEL_TOUCH_UI=1)
target_compile_options(goxel PRIVATE
    -include ${ROOT}/src/config.h
    -Wall -Wno-unknown-pragmas -Wno-unknown-warning-option
    $<$<COMPILE_LANGUAGE:C>:-std=gnu99>
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -Wno-narrowing>)

# Make sure ANativeActivity_onCreate from the glue doesn't get stripped.
set_target_properties(goxel PROPERTIES LINK_FLAGS
    "-u ANativeActivity_onCreate")

target_link_libraries(goxel native_app_glue android log EGL GLESv3 m)
