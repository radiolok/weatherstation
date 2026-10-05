cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_ota)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common.cmake)
