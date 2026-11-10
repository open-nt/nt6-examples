# A standalone project that uses an installed Nt:
#   cmake -S hello -B build-hello && cmake --build build-hello
cmake_minimum_required(VERSION 3.18)
project(hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Nt6 6 REQUIRED)

add_executable(hello hello.cpp)
target_link_libraries(hello PRIVATE Nt6::Core)
