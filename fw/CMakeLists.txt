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
  src/app_state.c
  src/shell_ws.c
  src/storage/storage.c
  src/storage/settings_ws.c
  src/display/display.c
  src/display/shell_sign.c
  src/io/io.c
  src/sensors/sensors.c
)

if(CONFIG_WS_NET)
  target_sources(app PRIVATE src/net/net_mgr.c src/net/timesync.c)
  if(CONFIG_WS_NET_WIFI)
    target_sources(app PRIVATE src/net/wifi_esp32.c)
  else()
    target_sources(app PRIVATE src/net/wifi_none.c)
  endif()
endif()

if(CONFIG_WS_NET AND CONFIG_HTTP_CLIENT)
  target_sources(app PRIVATE src/net/http_get.c)
endif()

target_sources_ifdef(CONFIG_WS_MQTT app PRIVATE src/mqtt/mqtt_svc.c)
target_sources_ifdef(CONFIG_WS_OTA app PRIVATE src/ota/ota.c)
target_sources_ifdef(CONFIG_WS_WATCHDOG app PRIVATE src/wdt/watchdog.c)

# METAR fallback: C++ adapter of au/metar_cpp + HTTPS fetch + root CAs
if(CONFIG_WS_METAR)
  ws_lib_add_metar(app)
  target_sources(app PRIVATE src/metar/metar.c)
  set(ca_c ${CMAKE_CURRENT_BINARY_DIR}/ws_gen/metar_ca.c)
  ws_embed_file(${WS_FW_DIR}/src/metar/ca_bundle.pem ws_metar_ca_bundle ${ca_c})
  target_sources(app PRIVATE ${ca_c})
endif()
target_sources_ifdef(CONFIG_WS_WEB app PRIVATE src/web/web.c src/web/api.c)

# Emulators behind the same aliases on native_sim (BME280 on i2c-emul,
# MH-Z19B on uart-emul, sign sniffer, button through gpio-emul)
if(CONFIG_WS_EMUL)
  target_include_directories(app PRIVATE emul)
  target_sources(app PRIVATE
    emul/bme280_emul.c
    emul/mhz19b_emul.c
    emul/sign_sniffer.c
    emul/shell_emul.c
  )
endif()

# Web UI: gzip resources served by the HTTP server
if(CONFIG_WS_WEB)
  include(${WS_FW_DIR}/web/web.cmake)
  ws_web_resources(app)
endif()
