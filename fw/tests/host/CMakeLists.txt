# Host build of fw/lib and the ztest suites in fw/tests/lib, using the
# ztest shim in include/zephyr/ztest.h. Not used by Zephyr builds.
#
#   cmake -S fw/tests/host -B build/host -G Ninja && ninja -C build/host
#   ctest --test-dir build/host --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(ws_host_tests C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(WS_FW_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${WS_FW_DIR}/lib/lib.cmake)

option(WS_SANITIZE "Build with ASan/UBSan" ON)
option(WS_COVERAGE "Build with gcov coverage" OFF)

add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation -g -O1 -DWS_HOST_TEST=1)
if(WS_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
if(WS_COVERAGE)
  add_compile_options(--coverage)
  add_link_options(--coverage)
endif()

add_library(ws_lib STATIC)
ws_lib_add(ws_lib)
target_include_directories(ws_lib PUBLIC ${WS_LIB_INCLUDE})
target_link_libraries(ws_lib PUBLIC m)

# METAR decoder (au/metar_cpp) and its adapter: C++ without exceptions and
# RTTI, as in the firmware (fw/modules/metar_cpp).
set(METAR_CPP_DIR ${WS_FW_DIR}/../au/metar_cpp)
file(GLOB metar_srcs ${METAR_CPP_DIR}/src/*.cpp)
target_sources(ws_lib PRIVATE ${metar_srcs} ${WS_FW_DIR}/lib/metar_map/metar_adapter.cpp)
target_include_directories(ws_lib PRIVATE ${METAR_CPP_DIR}/include)
set_source_files_properties(${metar_srcs} PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-Wno-error")
set_source_files_properties(${WS_FW_DIR}/lib/metar_map/metar_adapter.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti")
target_link_libraries(ws_lib PUBLIC stdc++)

enable_testing()
file(GLOB suites LIST_DIRECTORIES true ${WS_FW_DIR}/tests/lib/*)
foreach(dir ${suites})
  if(IS_DIRECTORY ${dir})
    get_filename_component(name ${dir} NAME)
    file(GLOB srcs ${dir}/src/*.c)
    add_executable(test_${name} ${srcs} ${CMAKE_CURRENT_LIST_DIR}/ztest_host.c)
    target_include_directories(test_${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include
                               ${WS_FW_DIR}/tests/golden ${dir}/src)
    target_link_libraries(test_${name} PRIVATE ws_lib)
    add_test(NAME ${name} COMMAND test_${name})
  endif()
endforeach()

# JSON helper for the web UI mock server (fw/tests/web/mock_server.py)
add_executable(wsapi ${CMAKE_CURRENT_LIST_DIR}/wsapi.c)
target_link_libraries(wsapi PRIVATE ws_lib)
