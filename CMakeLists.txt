cmake_minimum_required(VERSION 3.16)
project(qiansai C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_LVGL_UI "Enable LVGL touch UI with recording" ON)

if (NOT TARGET_SOC)
    set(TARGET_SOC "rk3588" CACHE STRING "NPU platform target")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknn_model_zoo/3rdparty 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknn_model_zoo/utils utils.out)

set(RETINAFACE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknn_model_zoo/examples/RetinaFace/cpp)

if (TARGET_SOC STREQUAL "rk1808" OR TARGET_SOC STREQUAL "rv1109" OR TARGET_SOC STREQUAL "rv1126")
    set(retinaface_src ${RETINAFACE_DIR}/rknpu1/retinaface.cc)
else()
    set(retinaface_src ${RETINAFACE_DIR}/rknpu2/retinaface.cc)
endif()

add_library(retinaface STATIC ${retinaface_src})

target_include_directories(retinaface PUBLIC
    ${RETINAFACE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknn_model_zoo/utils
)

target_link_libraries(retinaface fileutils imageutils)

set(qiansai_sources main.cpp serial_port.cpp)

if (ENABLE_LVGL_UI)
    list(APPEND qiansai_sources lvgl_ui.cpp recorder.cpp)
endif()

if (ENABLE_LVGL_UI)
    set(CONFIG_LV_USE_LINUX_DRM ON CACHE BOOL "Enable Linux DRM driver")
    set(CONFIG_LV_USE_EVDEV ON CACHE BOOL "Enable evdev input driver")
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples")
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lvgl lvgl_build)
endif()

add_executable(${PROJECT_NAME} ${qiansai_sources})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GST_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${RETINAFACE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknn_model_zoo/utils
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GST_LIBRARIES}
    ${GST_APP_LIBRARIES}
    retinaface
    fileutils
    imageutils
    imagedrawing
    ${LIBRKNNRT}
    dl
    m
    Threads::Threads
)

if (ENABLE_LVGL_UI)
    target_link_libraries(${PROJECT_NAME} PRIVATE lvgl::lvgl)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_EVDEV=1)
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GST_APP_CFLAGS_OTHER}
)
