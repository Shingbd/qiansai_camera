cmake_minimum_required(VERSION 3.16)
project(test_lvgl C CXX)
set(CMAKE_CXX_STANDARD 17)

set(CONFIG_LV_USE_LINUX_DRM ON CACHE BOOL "Enable Linux DRM driver")
set(CONFIG_LV_USE_EVDEV ON CACHE BOOL "Enable evdev input driver")
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples")
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/lvgl lvgl_build)

add_executable(test_lvgl main.cpp)
target_link_libraries(test_lvgl PRIVATE lvgl::lvgl)
target_compile_definitions(test_lvgl PRIVATE USE_EVDEV=1)
