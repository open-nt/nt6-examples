# A Qt 6 widgets application, as a Qt project writes it. It builds for the desktop with Qt 6 or Nt, and for Android
# with Nt (see "Nt on Android" in the Nt documentation):
#
#   cmake -S android -B build-android -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
#     -DCMAKE_PREFIX_PATH=<prefix of Nt for Android> -DNT_HOST_TOOLS_DIR=<directory with ntmoc and ntrcc>
#   cmake --build build-android            # makes build-android/qt_hello.apk
#   adb install build-android/qt_hello.apk
cmake_minimum_required(VERSION 3.18)
project(qt_hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets OpenGL OpenGLWidgets)
qt_standard_project_setup()

qt_add_executable(qt_hello main.cpp)
target_link_libraries(qt_hello PRIVATE Qt6::Widgets)

# OpenGL ES on Android: the window of the example of opengl/, clearing with OpenGL and painting with QPainter.
qt_add_executable(qt_glwindow ../opengl/glwindow.cpp)
target_link_libraries(qt_glwindow PRIVATE Qt6::OpenGL)

# QOpenGLWidget among ordinary widgets.
qt_add_executable(qt_glwidget glwidget.cpp)
target_link_libraries(qt_glwidget PRIVATE Qt6::OpenGLWidgets Qt6::Widgets)

# Fingers on a touch screen: QTouchEvent with several points, and a pinch.
qt_add_executable(qt_multitouch multitouch.cpp)
target_link_libraries(qt_multitouch PRIVATE Qt6::Widgets)

# A QVulkanWindow that clears with Vulkan, without shaders.
qt_add_executable(qt_vulkanwindow vulkanwindow.cpp)
target_link_libraries(qt_vulkanwindow PRIVATE Qt6::Gui)

# The Material 3 Expressive style of Nt, with the Nt logo as its icon (an option of Nt's own; Qt takes the icon from
# the resources of QT_ANDROID_PACKAGE_SOURCE_DIR).
qt_add_executable(qt_material MANUAL_FINALIZATION material.cpp)
target_link_libraries(qt_material PRIVATE Qt6::Widgets)
if(ANDROID AND COMMAND nt_android_add_apk)
    nt_android_add_apk(qt_material ICON nt-logo.png)
else()
    qt_finalize_target(qt_material)
endif()
# Google's icons, in a package of Nt's own whose icons are under Apache 2.0: with Nt, and if it is installed, the
# gallery links with it and shows icon buttons. An application that does so has to follow that license.
find_package(Nt6 QUIET)
if(TARGET Nt6::MaterialSymbols)
    target_link_libraries(qt_material PRIVATE Nt6::MaterialSymbols)
endif()
