cmake_minimum_required(VERSION 3.16)
project(WereadEinkBrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 查找 Qt6 包
find_package(Qt6 REQUIRED COMPONENTS Core Widgets WebEngineWidgets Network Test)

# Source files
set(SOURCES
    app/main.cpp
    app/shm_writer.cpp
    app/catalog_widget.cpp
    app/eink_refresh.cpp
    app/resource_interceptor.cpp
    app/routed_page.cpp
    app/smart_refresh.cpp
    app/touch_logger.cpp
    app/gesture_filter.cpp
    app/weread_browser.cpp
    app/weread_browser_ctor.cpp
    app/weread_browser_init_profile.cpp
    app/weread_browser_init_load.cpp
    app/weread_browser_init_nav.cpp
    app/weread_browser_init_startup.cpp
    app/weread_browser_part1.cpp
    app/weread_browser_part2.cpp
    app/weread_browser_part3.cpp
    app/weread_browser_part4.cpp
    app/weread_browser_part5.cpp
    app/weread_browser_part6.cpp
)

# 添加可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/../archive/bridge-mvp
)

# 链接 Qt6 库
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::Network
    Qt6::Test
)

# 安装配置
install(TARGETS ${PROJECT_NAME} DESTINATION /opt/bin)
