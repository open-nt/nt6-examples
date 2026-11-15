# MIT License - see LICENSE
#
# Nt on an emulated board: a self-checking application that needs no hardware, and a web page with its results.
# The checks use the loopback interface, so they need no network set up. Build and run it with west:
#
#   west build -b mps2/an385 zephyr/qemu -t run -- -DNT_ROOT=/path/to/Nt
cmake_minimum_required(VERSION 3.20)

# This repository is not a west workspace, so Nt is named as a module: pass -DNT_ROOT=<a checkout of Nt>
# (or set NT_ROOT in the environment). An application of its own lists Nt in its west manifest instead,
# and needs none of these lines.
set(NT_ROOT "$ENV{NT_ROOT}" CACHE PATH "A checkout of Nt, used as a Zephyr module")
if(NT_ROOT)
    list(APPEND ZEPHYR_EXTRA_MODULES ${NT_ROOT})
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nt_zephyr_qemu LANGUAGES CXX)

target_sources(app PRIVATE src/main.cpp src/webpage.cpp src/board.cpp)
target_link_libraries(app PRIVATE Nt::Core Nt::Network)
if(CONFIG_NT_HTTPSERVER)
    target_link_libraries(app PRIVATE Nt::HttpServer)
endif()
