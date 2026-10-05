# Builds au/metar_cpp as a Zephyr library when CONFIG_METAR_CPP=y.
if(CONFIG_METAR_CPP)
  set(METAR_CPP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../au/metar_cpp)
  zephyr_library_named(metar_cpp)
  zephyr_library_sources(
    ${METAR_CPP_DIR}/src/Metar.cpp
    ${METAR_CPP_DIR}/src/Clouds.cpp
    ${METAR_CPP_DIR}/src/Phenom.cpp
    ${METAR_CPP_DIR}/src/RunwayState.cpp
    ${METAR_CPP_DIR}/src/Utils.cpp
  )
  zephyr_include_directories(${METAR_CPP_DIR}/include)
  zephyr_library_compile_options(-fno-exceptions -fno-rtti)
endif()
