# A standalone project that uses an installed Nt:
#   cmake -S comms -B build-comms && cmake --build build-comms
#
# For an Android device, build Nt from source with the NDK together with this program (a cross build of Nt has no
# install step, as on Zephyr). ntmoc and ntrcc are built for the build machine automatically:
#   cmake -S comms -B build-android -G Ninja -DNT_ROOT=<path of the Nt checkout> \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
#   cmake --build build-android
#   adb push build-android/comms_chat /data/local/tmp && adb shell /data/local/tmp/comms_chat --help
cmake_minimum_required(VERSION 3.18)
project(comms_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NT_ROOT "" CACHE PATH "A checkout of Nt to build with this program, instead of an installed Nt")
if(NT_ROOT)
    set(NT_BUILD_TESTS OFF)
    add_subdirectory("${NT_ROOT}" nt EXCLUDE_FROM_ALL)
    set(_nt_comms Nt::Comms)
else()
    find_package(Nt6 6 REQUIRED COMPONENTS Core Network Comms)
    set(_nt_comms Nt6::Comms)
endif()

add_executable(comms_chat chat.cpp)
target_link_libraries(comms_chat PRIVATE ${_nt_comms})
