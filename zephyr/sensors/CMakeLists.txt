# MIT License - see LICENSE
#
# The sensors of a Zephyr board, listed and read every second with Nt Sensors. Build it for the MIMXRT1170-EVKB
# with west, for example:
#
#   west build -b mimxrt1170_evk/mimxrt1176/cm7 zephyr/sensors -- -DNT_ROOT=/path/to/Nt
cmake_minimum_required(VERSION 3.20)

# This repository is not a west workspace, so Nt is named as a module: pass -DNT_ROOT=<a checkout of Nt>
# (or set NT_ROOT in the environment).
set(NT_ROOT "$ENV{NT_ROOT}" CACHE PATH "A checkout of Nt, used as a Zephyr module")
if(NT_ROOT)
    list(APPEND ZEPHYR_EXTRA_MODULES ${NT_ROOT})
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nt_zephyr_sensors LANGUAGES CXX)

target_sources(app PRIVATE src/main.cpp)
target_link_libraries(app PRIVATE Nt::Core Nt::Sensors)
