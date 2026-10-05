# Web UI resources: gzip-compressed and compiled into the firmware.
# Budget: 80 KB gzip in total (spec section 11), checked by fw/web/test.

set(WS_WEB_SRC ${CMAKE_CURRENT_LIST_DIR}/src)
set(WS_WEB_FILES index.html app.js render.js style.css)

function(ws_web_resources target)
  zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/sections-rom.ld)
  set(gen ${ZEPHYR_BINARY_DIR}/include/generated)
  foreach(f ${WS_WEB_FILES})
    generate_inc_file_for_target(${target} ${WS_WEB_SRC}/${f} ${gen}/${f}.gz.inc --gzip)
  endforeach()
  # glyphs.json for /api/glyphs (not compressed: user pictograms are appended)
  set(glyphs_c ${CMAKE_CURRENT_BINARY_DIR}/ws_gen/glyphs_json.c)
  ws_embed_file(${WS_WEB_SRC}/glyphs.json ws_glyphs_json ${glyphs_c})
  target_sources(${target} PRIVATE ${glyphs_c})
endfunction()
