cmake_minimum_required(VERSION 3.16)
project(epaper_shm_viewer LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Gui Network)

add_executable(epaper_shm_viewer
    main.cpp
)

target_include_directories(epaper_shm_viewer PRIVATE ../)
target_link_libraries(epaper_shm_viewer PRIVATE Qt6::Quick Qt6::Gui)
target_link_libraries(epaper_shm_viewer PRIVATE Qt6::Network)
