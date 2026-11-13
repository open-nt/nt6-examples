# MIT License - see LICENSE
#
# All the Nt examples, built against an installed Nt such as the snap:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=/snap/open-nt6/current/usr
#   cmake --build build
#   ctest --test-dir build
#
# hello/, signals/ and widgets/ are also projects of their own: `cmake -S hello -B build-hello`.
# qt/ builds the widget examples against Qt 6 instead and is not part of this project.
cmake_minimum_required(VERSION 3.18)
project(Nt6Examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The snap's default location, so that a plain `cmake -S . -B build` finds it too.
list(APPEND CMAKE_PREFIX_PATH /snap/open-nt6/current/usr)
find_package(Nt6 6 REQUIRED)

enable_testing()

# The smallest program: a QCoreApplication and a timer.
add_subdirectory(hello)

# The patterns of the "Signals and Slots" guide; it checks itself.
add_subdirectory(signals)
add_test(NAME signals COMMAND signals)

# A form, a tour of the widgets and a program that renders each widget to a picture.
add_subdirectory(widgets)

# Same flow as a Qt project using CMAKE_AUTOMOC.
add_executable(moc_example moc/main.cpp moc/shape.h)
target_link_libraries(moc_example PRIVATE Nt6::Core)
nt_enable_automoc(moc_example)

# A test written the way a Qt project writes one, with the .moc file included.
add_executable(tst_counter test/tst_counter.cpp)
target_link_libraries(tst_counter PRIVATE Nt6::Test)
nt_enable_automoc(tst_counter)
add_test(NAME tst_counter COMMAND tst_counter)

# A window that is painted with QPainter; see "Using Nt GUI" in the documentation.
add_executable(hello_window gui/hello_window.cpp)
target_link_libraries(hello_window PRIVATE Nt6::Gui)

# A QPainter benchmark (ruler, circles, bezier graphs, bars, icons & text) that prints frame rates; the raster
# and the OpenGL paint engine can be switched between at run time.
add_executable(painter_bench gui/painter_bench.cpp)
target_link_libraries(painter_bench PRIVATE Nt6::Gui Nt6::OpenGL)

# The pixel art of the platform game, drawn on one sheet for review.
add_executable(sprite_sheet platformer/sprite_sheet.cpp platformer/sprites.h)
target_link_libraries(sprite_sheet PRIVATE Nt6::Gui)

# A platform game: Nt Gamepad for the controls, Nt FM for the sound effects and for the MIDI soundtrack.
add_executable(mochimaru platformer/mochimaru.cpp platformer/sprites.h)
target_link_libraries(mochimaru PRIVATE Nt6::Gui Nt6::OpenGL Nt6::Gamepad Nt6::FM xcb)

# Shows an SVG file in a window, or converts what it draws; see "Using Nt SVG" in the documentation.
add_executable(svg_viewer svg/svg_viewer.cpp)
target_link_libraries(svg_viewer PRIVATE Nt6::SvgWidgets)

# A serial terminal; see "Using Nt Serial Port" in the documentation.
add_executable(serialterm serialport/serialterm.cpp)
target_link_libraries(serialterm PRIVATE Nt6::SerialPort)

# An echo server and client; see "Using Nt WebSockets" in the documentation.
add_executable(websocket_echo websockets/echo.cpp)
target_link_libraries(websocket_echo PRIVATE Nt6::WebSockets)

# A small web service; see "Using Nt HTTP Server" in the documentation.
add_executable(hello_server httpserver/hello_server.cpp)
target_link_libraries(hello_server PRIVATE Nt6::HttpServer)

# A clock on the session bus; see "Using Nt D-Bus" in the documentation.
add_executable(dbus_clock dbus/clock.cpp)
target_link_libraries(dbus_clock PRIVATE Nt6::DBus)
nt_enable_automoc(dbus_clock)
