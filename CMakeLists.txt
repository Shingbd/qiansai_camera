cmake_minimum_required(VERSION 3.16)
project(qiansai C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


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

add_executable(${PROJECT_NAME} main.cpp)

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

target_compile_options(${PROJECT_NAME} PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GST_APP_CFLAGS_OTHER}
)
