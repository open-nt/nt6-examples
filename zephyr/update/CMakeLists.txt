# MIT License - see LICENSE
#
# Firmware updates over the air with Nt Device: the board runs under a watchdog, confirms a new image once it has
# reached the network, and downloads a newer signed image, which MCUboot boots after a reset. Set the URL in
# src/main.cpp, then build it with MCUboot for the MIMXRT1170-EVKB:
#
#   west build --sysbuild -b mimxrt1170_evk/mimxrt1176/cm7 zephyr/update -- -DZEPHYR_EXTRA_MODULES=/path/to/Nt
#
# (with sysbuild, -DNT_ROOT would only reach the sysbuild level; ZEPHYR_EXTRA_MODULES reaches every image)
#
# and serve a newer build's build/update/zephyr/zephyr.signed.bin (raise VERSION first) from that URL.
cmake_minimum_required(VERSION 3.20)

# This repository is not a west workspace, so Nt is named as a module: pass -DNT_ROOT=<a checkout of Nt>
# (or set NT_ROOT in the environment).
set(NT_ROOT "$ENV{NT_ROOT}" CACHE PATH "A checkout of Nt, used as a Zephyr module")
if(NT_ROOT)
    list(APPEND ZEPHYR_EXTRA_MODULES ${NT_ROOT})
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nt_zephyr_update LANGUAGES CXX)

target_sources(app PRIVATE src/main.cpp)
target_link_libraries(app PRIVATE Nt::Core Nt::Network Nt::Device)
