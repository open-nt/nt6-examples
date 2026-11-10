# MIT License - see LICENSE
#
# A standalone project that shows the signal and slot patterns of the "Signals and Slots" guide:
#
#   cmake -S signals -B build-signals && cmake --build build-signals
#   build-signals/signals
cmake_minimum_required(VERSION 3.18)
project(NtSignals LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Nt6 6 REQUIRED)

# List the header: ntmoc must see the Q_OBJECT classes in it.
add_executable(signals main.cpp thermostat.h)
target_link_libraries(signals PRIVATE Nt6::Core)
nt_enable_automoc(signals)
