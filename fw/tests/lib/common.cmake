# Shared setup for the ztest suites of fw/lib (native_sim, twister).
set(WS_FW_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${WS_FW_DIR}/lib/lib.cmake)
ws_lib_add(app)
file(GLOB app_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE ${WS_FW_DIR}/tests/golden ${CMAKE_CURRENT_SOURCE_DIR}/src)
