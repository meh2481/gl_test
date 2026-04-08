# cmake/toolchains/android.cmake
# Android cross-compilation toolchain wrapper.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android.cmake \
#         -DANDROID_NDK=/path/to/ndk \
#         [-DANDROID_ABI=arm64-v8a] \
#         [-DANDROID_PLATFORM=android-26] \
#         ...
#
# ANDROID_NDK is read from the env vars ANDROID_NDK_ROOT / ANDROID_NDK_HOME
# when not explicitly passed on the command line.

if(NOT DEFINED ANDROID_NDK)
    if(DEFINED ENV{ANDROID_NDK_ROOT} AND NOT "$ENV{ANDROID_NDK_ROOT}" STREQUAL "")
        set(ANDROID_NDK "$ENV{ANDROID_NDK_ROOT}" CACHE PATH "Android NDK root directory")
    elseif(DEFINED ENV{ANDROID_NDK_HOME} AND NOT "$ENV{ANDROID_NDK_HOME}" STREQUAL "")
        set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}" CACHE PATH "Android NDK root directory")
    elseif(DEFINED ENV{ANDROID_NDK} AND NOT "$ENV{ANDROID_NDK}" STREQUAL "")
        set(ANDROID_NDK "$ENV{ANDROID_NDK}" CACHE PATH "Android NDK root directory")
    endif()
endif()

if(NOT DEFINED ANDROID_NDK OR "${ANDROID_NDK}" STREQUAL "")
    message(FATAL_ERROR
        "ANDROID_NDK must be set to the Android NDK root directory.\n"
        "Pass -DANDROID_NDK=/path/to/ndk or set the ANDROID_NDK_ROOT environment variable.")
endif()

if(NOT DEFINED ANDROID_ABI)
    set(ANDROID_ABI "arm64-v8a" CACHE STRING "Android target ABI (arm64-v8a or x86_64)")
endif()

if(NOT DEFINED ANDROID_PLATFORM)
    set(ANDROID_PLATFORM "android-26" CACHE STRING "Android minimum API level")
endif()

if(NOT DEFINED ANDROID_STL)
    set(ANDROID_STL "c++_static" CACHE STRING "Android C++ STL variant")
endif()

set(NDK_TOOLCHAIN_FILE "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS "${NDK_TOOLCHAIN_FILE}")
    message(FATAL_ERROR
        "NDK toolchain file not found: ${NDK_TOOLCHAIN_FILE}\n"
        "Make sure ANDROID_NDK points to a valid NDK installation.")
endif()

include("${NDK_TOOLCHAIN_FILE}")
