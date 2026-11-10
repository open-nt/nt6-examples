# The widget examples built against Qt 6 instead of Nt, from the very same sources, to compare the two:
#   cmake -S qt -B build-qt && cmake --build build-qt
# It needs Qt 6 and is not part of the top-level project, because Nt provides the Qt6:: names itself.
cmake_minimum_required(VERSION 3.18)
project(nt_examples_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets SvgWidgets)

# The logo of both examples is an SVG file in a resource, shown by a QSvgWidget.
qt_add_resources(hello_widgets_resources ../widgets/hello_widgets.qrc)
add_executable(hello_widgets_qt ../widgets/hello_widgets.cpp ${hello_widgets_resources})
target_link_libraries(hello_widgets_qt PRIVATE Qt6::SvgWidgets)

qt_add_resources(gallery_resources ../widgets/gallery.qrc)
add_executable(gallery_qt ../widgets/gallery.cpp ${gallery_resources})
target_link_libraries(gallery_qt PRIVATE Qt6::SvgWidgets)
