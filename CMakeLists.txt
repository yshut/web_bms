cmake_minimum_required(VERSION 3.10)
project(app_lvgl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -g")

# 添加 pthread 支持
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")

# LVGL 源文件路径 (假设 LVGL 库已经在系统中或者在 package/gui/lvgl 中)
# 这里需要根据实际路径调整
set(LVGL_DIR ${CMAKE_SOURCE_DIR}/../gui/lvgl-8.3)
set(LV_DRIVERS_DIR ${CMAKE_SOURCE_DIR}/../gui/lv_drivers)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${LVGL_DIR}
    ${LV_DRIVERS_DIR}
)

# 收集所有源文件
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.c
)

# LVGL 源文件 (简化，实际应该包含所有 LVGL 源文件)
file(GLOB_RECURSE LVGL_SOURCES
    ${LVGL_DIR}/src/*.c
)

# LV_DRIVERS 源文件
file(GLOB_RECURSE LV_DRIVERS_SOURCES
    ${LV_DRIVERS_DIR}/display/fbdev.c
    ${LV_DRIVERS_DIR}/indev/evdev.c
)

# 创建可执行文件
add_executable(app_lvgl
    ${SOURCES}
    ${LVGL_SOURCES}
    ${LV_DRIVERS_SOURCES}
)

# 链接库
target_link_libraries(app_lvgl
    pthread
    m
)

# 安装规则
install(TARGETS app_lvgl
    RUNTIME DESTINATION /usr/bin
)

