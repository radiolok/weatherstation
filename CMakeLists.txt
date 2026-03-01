cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(weather_station)

set(INCLUDE_LIB ${CMAKE_SOURCE_DIR}/include)

include_directories(${INCLUDE_LIB})

set (SOURCES
        ${INCLUDE_LIB}/defines.h
        include/wifi.h
        src/wifi.c
        src/main.c
)

target_sources(app PRIVATE ${SOURCES})