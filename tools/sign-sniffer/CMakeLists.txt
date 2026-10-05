# Bench sniffer for the sign line, built from the firmware's own fw/lib.
#   cmake -S tools/sign-sniffer -B build/sign-sniffer -G Ninja && ninja -C build/sign-sniffer
cmake_minimum_required(VERSION 3.20)
project(sign_sniffer C)

set(CMAKE_C_STANDARD 11)
set(WS_FW_DIR ${CMAKE_CURRENT_LIST_DIR}/../../fw)
include(${WS_FW_DIR}/lib/lib.cmake)

add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation -O2 -D_DEFAULT_SOURCE)

# Only fw/lib/sign is needed: the decoder, the splitter and the frame diff.
add_library(ws_sign STATIC ${WS_LIB_DIR}/sign/sign.c ${WS_LIB_DIR}/sign/glyphs.c)
target_include_directories(ws_sign PUBLIC ${WS_LIB_INCLUDE})
target_link_libraries(ws_sign PUBLIC m)

add_executable(sign-sniffer sign_sniffer.c)
target_link_libraries(sign-sniffer PRIVATE ws_sign)
