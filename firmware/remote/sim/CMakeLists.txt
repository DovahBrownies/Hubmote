# Hubmote's UI on a PC (sim/README.md). Normally built by sim/run_sim.py.
cmake_minimum_required(VERSION 3.16)
project(hubmote_sim C)

set(REMOTE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(LVGL_DIR "${REMOTE_DIR}/managed_components/lvgl__lvgl" CACHE PATH "LVGL 9.6 sources")
set(CJSON_DIR "" CACHE PATH "cJSON sources (cJSON.c and cJSON.h)")
set(SIM_W 1024 CACHE STRING "screen width")
set(SIM_H 600 CACHE STRING "screen height")

if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
    message(FATAL_ERROR "no LVGL at ${LVGL_DIR}: build the firmware once, or pass -DLVGL_DIR")
endif()
if(NOT EXISTS "${CJSON_DIR}/cJSON.c")
    message(FATAL_ERROR "no cJSON at '${CJSON_DIR}': pass -DCJSON_DIR")
endif()

set(CMAKE_C_STANDARD 11)

# LVGL, configured like the firmware (lv_kconfig.h)
file(GLOB_RECURSE LVGL_SRCS "${LVGL_DIR}/src/*.c")
add_library(lvgl STATIC ${LVGL_SRCS})
target_include_directories(lvgl PUBLIC "${LVGL_DIR}" "${LVGL_DIR}/src" "${LVGL_DIR}/.." "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(lvgl PUBLIC "LV_CONF_KCONFIG_EXTERNAL_INCLUDE=<lv_kconfig.h>")
target_compile_options(lvgl PRIVATE -w -O2)

# The UI as on the remote, with stand-ins for ESP-IDF and the module platform
file(GLOB UI_SRCS "${REMOTE_DIR}/components/ui/*.c")
set(FONT_DIR "${REMOTE_DIR}/components/ui/fonts/h${SIM_H}")
if(NOT EXISTS "${FONT_DIR}")
    message(FATAL_ERROR "no fonts for ${SIM_H} px high: tools/fonts/build_fonts.py --height ${SIM_H}")
endif()
file(GLOB FONT_SRCS "${FONT_DIR}/*.c")
add_executable(hubmote_sim
    sim_main.c sim_hub.c
    ${UI_SRCS} ${FONT_SRCS}
    "${REMOTE_DIR}/components/hub/hub_tz.c"
    "${REMOTE_DIR}/modules/weather/weather_ui.c"
    "${REMOTE_DIR}/modules/weather/weather_data.c"
    "${REMOTE_DIR}/modules/sleep_timer/sleep_timer.c"
    "${REMOTE_DIR}/modules/screen_time/screen_time.c"
    "${REMOTE_DIR}/modules/guest_wifi/guest_wifi.c"
    "${REMOTE_DIR}/modules/speed_test/speed_test.c"
    "${CJSON_DIR}/cJSON.c")
target_include_directories(hubmote_sim PRIVATE
    stubs
    "${REMOTE_DIR}/components/ui"
    "${REMOTE_DIR}/components/ui/include"
    "${REMOTE_DIR}/components/hub/include"
    "${REMOTE_DIR}/components/hub"
    "${REMOTE_DIR}/modules/weather"
    "${CJSON_DIR}")
target_compile_definitions(hubmote_sim PRIVATE
    SIM_W=${SIM_W} SIM_H=${SIM_H} UI_W=${SIM_W} UI_H=${SIM_H}
    "SIM_FIXTURE=\"${REMOTE_DIR}/modules/weather/test/forecast_sydney.json\"")
target_compile_options(hubmote_sim PRIVATE -Wall -O1 -include "${CMAKE_CURRENT_LIST_DIR}/stubs/sim_compat.h")
target_link_libraries(hubmote_sim lvgl m)
