cmake_minimum_required(VERSION 3.16)
project(DisplayTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# libdrm
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)

add_executable(display-test
    display_test.cpp
)

target_link_libraries(display-test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${DRM_LIBRARIES}
)

target_include_directories(display-test PRIVATE
    ${DRM_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../app
)

target_compile_options(display-test PRIVATE
    -Wall -Wextra
)
