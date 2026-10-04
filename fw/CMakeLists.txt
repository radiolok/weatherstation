# Weatherstation firmware (Zephyr application).
#
#   west build -b esp32s3_devkitc/esp32s3/procpu --sysbuild fw
#   west build -b native_sim/native/64 fw
cmake_minimum_required(VERSION 3.20.0)

set(WS_FW_DIR ${CMAKE_CURRENT_LIST_DIR})
list(APPEND ZEPHYR_EXTRA_MODULES ${WS_FW_DIR}/modules/metar_cpp)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(weatherstation C CXX)

include(${WS_FW_DIR}/lib/lib.cmake)
ws_lib_add(app)

target_include_directories(app PRIVATE src)
target_sources(app PRIVATE
  src/main.c
)
