# Pure C logic of the firmware (no Zephyr services).
#
# Used by the Zephyr application, by the ztest suites in fw/tests/lib and by
# the host build in fw/tests/host. Usage:
#
#   include(${WS_FW_DIR}/lib/lib.cmake)
#   ws_lib_add(<target>)

set(WS_LIB_DIR ${CMAKE_CURRENT_LIST_DIR})

file(GLOB WS_LIB_SOURCES CONFIGURE_DEPENDS ${WS_LIB_DIR}/*/*.c)
set(WS_LIB_INCLUDE ${WS_LIB_DIR}/include)

# Turn a text/binary file into a C source defining
#   const unsigned char <symbol>[]; const unsigned int <symbol>_len;
# The data is NUL-terminated so text can be used as a C string.
function(ws_embed_file input symbol output)
  add_custom_command(
    OUTPUT ${output}
    COMMAND ${CMAKE_COMMAND} -DIN=${input} -DSYM=${symbol} -DOUT=${output}
            -P ${WS_LIB_DIR}/embed.cmake
    DEPENDS ${input} ${WS_LIB_DIR}/embed.cmake
    COMMENT "Embedding ${input}"
  )
endfunction()

set(WS_FACTORY_JSON ${WS_LIB_DIR}/screens/factory_screens.json)

function(ws_lib_add target)
  set(gen ${CMAKE_CURRENT_BINARY_DIR}/ws_gen/factory_screens.c)
  ws_embed_file(${WS_FACTORY_JSON} ws_factory_screens_json ${gen})
  target_sources(${target} PRIVATE ${WS_LIB_SOURCES} ${gen})
  target_include_directories(${target} PRIVATE ${WS_LIB_INCLUDE})
endfunction()
