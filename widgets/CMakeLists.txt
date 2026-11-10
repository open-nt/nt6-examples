# MIT License - see LICENSE
#
# The widget examples as a standalone project, built against an installed Nt such as the snap:
#
#   sudo snap install open-nt6
#   sudo apt install cmake g++ libssl-dev libfreetype-dev libfontconfig-dev libxcb1-dev
#   cmake -S widgets -B build-widgets -DCMAKE_PREFIX_PATH=/snap/open-nt6/current/usr
#   cmake --build build-widgets
#   build-widgets/hello_widgets
#
# Nt's libraries are static, so the OpenSSL, FreeType, fontconfig and XCB development packages are needed to link
# them. Without a display, run the examples with `-platform offscreen`.
#
# hello_widgets and gallery show the Nt logo with Nt SVG, which is part of Nt from 6.0.0-rc3.
cmake_minimum_required(VERSION 3.18)
project(NtWidgetsExamples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The snap's default location, so that a plain `cmake -S widgets -B build` finds it too.
list(APPEND CMAKE_PREFIX_PATH /snap/open-nt6/current/usr)
find_package(Nt6 6 REQUIRED)

# A form made of widgets and layouts; see "Using Nt Widgets" in the documentation. Its logo, nt-logo.svg, is
# compiled into the program from hello_widgets.qrc and shown by a QSvgWidget.
qt_add_resources(hello_widgets_resources hello_widgets.qrc)
add_executable(hello_widgets hello_widgets.cpp ${hello_widgets_resources})
target_link_libraries(hello_widgets PRIVATE Nt6::SvgWidgets)

# A tour of the widgets: layouts, controls, custom widgets, a dark theme. It shows the same logo, from gallery.qrc.
qt_add_resources(gallery_resources gallery.qrc)
add_executable(gallery gallery.cpp ${gallery_resources})
target_link_libraries(gallery PRIVATE Nt6::SvgWidgets)

# Renders each widget in its states to <directory>/widgets/*.png: `render_widgets <directory>`.
add_executable(render_widgets render_widgets.cpp)
target_link_libraries(render_widgets PRIVATE Nt6::Widgets)
