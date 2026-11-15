# MIT License - see LICENSE
#
# All the Nt examples, built against an installed Nt such as the snap:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=/snap/open-nt6/current/usr
#   cmake --build build
#   ctest --test-dir build
#
# hello/, signals/ and widgets/ are also projects of their own: `cmake -S hello -B build-hello`.
# qt/ builds the widget examples against Qt 6 instead and is not part of this project; zephyr/ holds Zephyr
# applications built with west (see zephyr/hello/CMakeLists.txt). Programs that need a module the installed Nt
# was built without (Vulkan, Multimedia, ...) are left out with a message.
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

# Paths, gradients, pens and transformations, antialiased, in software or with OpenGL (painting --gl); it also renders
# the pictures of the "Painting" guide of Nt (painting --write <Nt>/doc/images) and times the scenes (painting --bench).
add_executable(painting gui/painting.cpp)
target_link_libraries(painting PRIVATE Nt6::OpenGL)

# An OpenGL window painted with OpenGL and QPainter, with or without vsync; see QOpenGLWindow.
add_executable(glwindow opengl/glwindow.cpp)
target_link_libraries(glwindow PRIVATE Nt6::OpenGL)

# The pixel art of the platform game, drawn on one sheet for review.
add_executable(sprite_sheet platformer/sprite_sheet.cpp platformer/sprites.h)
target_link_libraries(sprite_sheet PRIVATE Nt6::Gui)

# A platform game: Nt Gamepad for the controls, Nt FM for the sound effects and for the MIDI soundtrack.
add_executable(mochimaru platformer/mochimaru.cpp platformer/sprites.h)
target_link_libraries(mochimaru PRIVATE Nt6::Gui Nt6::OpenGL Nt6::Gamepad Nt6::FM xcb)

# A progressive path tracer on the Vulkan ray tracing pipeline, with NVIDIA shader execution reordering, ray tracing
# position fetch and procedural spheres; see "Using Nt Vulkan" in the documentation. The SPIR-V shaders are checked in
# (raytracing/shaders/compile.sh rebuilds them with glslc). Needs an Nt built with the Vulkan headers.
nt_add_resources(pathtracer_resources raytracing/shaders/pathtracer.qrc)
add_executable(pathtracer raytracing/pathtracer.cpp ${pathtracer_resources})
target_link_libraries(pathtracer PRIVATE Nt6::Gui)

# DLSS Ray Reconstruction is an opt-in build against NVIDIA's DLSS SDK, which is proprietary and not part of this
# repository: clone https://github.com/NVIDIA/DLSS (its licence applies) and pass -DDLSS_SDK=<that checkout>. The path
# tracer then offers --dlss and the N key on RTX GPUs; the feature library is loaded from the SDK's lib/Linux_x86_64/rel.
set(DLSS_SDK "" CACHE PATH "A checkout of NVIDIA's DLSS SDK, to build the path tracer with DLSS Ray Reconstruction")
if(DLSS_SDK AND EXISTS "${DLSS_SDK}/include/nvsdk_ngx_helpers_dlssd_vk.h" AND EXISTS "${DLSS_SDK}/lib/Linux_x86_64/libnvsdk_ngx.a")
    target_compile_definitions(pathtracer PRIVATE HAVE_DLSS DLSS_SDK_LIB_DIR="${DLSS_SDK}/lib/Linux_x86_64/rel")
    target_include_directories(pathtracer SYSTEM PRIVATE "${DLSS_SDK}/include")
    target_link_libraries(pathtracer PRIVATE "${DLSS_SDK}/lib/Linux_x86_64/libnvsdk_ngx.a" ${CMAKE_DL_LIBS})
    message(STATUS "pathtracer: DLSS Ray Reconstruction from ${DLSS_SDK}")
elseif(DLSS_SDK)
    message(WARNING "DLSS_SDK=${DLSS_SDK} lacks include/nvsdk_ngx_helpers_dlssd_vk.h or lib/Linux_x86_64/libnvsdk_ngx.a")
endif()

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

# A triangle rendered with hardware ray tracing in a QVulkanWindow; see "Using Nt Vulkan" in the documentation. The
# SPIR-V shaders are checked in (vulkan/shaders/compile.sh rebuilds them with glslc). Needs an Nt built with the Vulkan
# headers, like the path tracer.
nt_add_resources(rtriangle_resources vulkan/shaders/rtriangle.qrc)
add_executable(rtriangle vulkan/rtriangle.cpp ${rtriangle_resources})
target_link_libraries(rtriangle PRIVATE Nt6::Gui)

# A live dashboard of charts, and the program that renders the pictures of the "Using Nt Charts" guide
# (render_charts <Nt>/doc/images).
if(TARGET Nt6::Charts)
    add_executable(chart_dashboard charts/dashboard.cpp)
    target_link_libraries(chart_dashboard PRIVATE Nt6::Charts)
    add_executable(render_charts charts/render_charts.cpp)
    target_link_libraries(render_charts PRIVATE Nt6::Charts)
else()
    message(STATUS "No Nt6::Charts: chart_dashboard and render_charts are left out")
endif()

# Scripting with QJSEngine, and a QML document with a C++ type; see "Using Nt Qml" and "Using Nt QML".
if(TARGET Nt6::Qml)
    add_executable(js_scripting qml/scripting.cpp)
    target_link_libraries(js_scripting PRIVATE Nt6::Qml)
    nt_enable_automoc(js_scripting)
    add_executable(hello_qml qml/hello_qml.cpp)
    target_link_libraries(hello_qml PRIVATE Nt6::Qml)
    nt_enable_automoc(hello_qml)
else()
    message(STATUS "No Nt6::Qml: js_scripting and hello_qml are left out")
endif()

# An address book in an SQLite database; see "Using Nt SQL" in the documentation.
if(TARGET Nt6::Sql)
    add_executable(contacts sql/contacts.cpp)
    target_link_libraries(contacts PRIVATE Nt6::Sql)
else()
    message(STATUS "No Nt6::Sql: contacts is left out")
endif()

# A push-to-talk switch on a GPIO line; see "Using Nt GPIO" in the documentation.
if(TARGET Nt6::Gpio)
    add_executable(ptt gpio/ptt.cpp)
    target_link_libraries(ptt PRIVATE Nt6::Gpio)
else()
    message(STATUS "No Nt6::Gpio: ptt is left out")
endif()

# Prints what the gamepads do; see "Using Nt Gamepad" in the documentation.
if(TARGET Nt6::Gamepad)
    add_executable(gamepadinfo gamepad/gamepadinfo.cpp)
    target_link_libraries(gamepadinfo PRIVATE Nt6::Gamepad)
else()
    message(STATUS "No Nt6::Gamepad: gamepadinfo is left out")
endif()

# Prints what a glTF file holds; see "Using Nt glTF" in the documentation.
if(TARGET Nt6::Gltf)
    add_executable(gltfinfo gltf/gltfinfo.cpp)
    target_link_libraries(gltfinfo PRIVATE Nt6::Gltf)
else()
    message(STATUS "No Nt6::Gltf: gltfinfo is left out")
endif()

# Audio devices and a tone, WAV playback, decoding, recording, the camera and a viewfinder; see "Using Nt Multimedia".
if(TARGET Nt6::Multimedia)
    foreach(program audiodevices audioplayer camera recorder decoder)
        add_executable(${program} multimedia/${program}.cpp)
        target_link_libraries(${program} PRIVATE Nt6::Multimedia)
    endforeach()
    if(TARGET Nt6::MultimediaWidgets)
        add_executable(viewfinder multimedia/viewfinder.cpp)
        target_link_libraries(viewfinder PRIVATE Nt6::MultimediaWidgets)
    endif()
else()
    message(STATUS "No Nt6::Multimedia: the multimedia examples are left out")
endif()

# FM sound effects and chip music, from C++ and from a QML document; see "Using Nt FM" in the documentation.
if(TARGET Nt6::FM)
    add_executable(fmplay fm/fmplay.cpp)
    target_link_libraries(fmplay PRIVATE Nt6::FM)
    if(TARGET Nt6::FMQml)
        add_executable(fmqml fm/fmqml.cpp)
        target_link_libraries(fmqml PRIVATE Nt6::FMQml)
    endif()
else()
    message(STATUS "No Nt6::FM: fmplay and fmqml are left out")
endif()
