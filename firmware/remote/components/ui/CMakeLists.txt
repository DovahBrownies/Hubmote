# The fonts drawn for this screen's height (tools/fonts/build_fonts.py --height).
set(FONT_DIR "${CMAKE_CURRENT_LIST_DIR}/fonts/h${CONFIG_HM_SCREEN_H}")
if(CONFIG_HM_SCREEN_H AND NOT EXISTS "${FONT_DIR}")
    message(FATAL_ERROR "No fonts for a ${CONFIG_HM_SCREEN_H} px high screen: run tools/fonts/build_fonts.py --height ${CONFIG_HM_SCREEN_H}")
endif()
file(GLOB FONT_SRCS "${FONT_DIR}/*.c")

idf_component_register(SRCS "ui.c" "ui_kit.c" "ui_apps.c" "ui_topbar.c" "ui_rail.c" "ui_pad.c"
                            "ui_tv_home.c" "ui_app.c" "ui_keyboard.c" "ui_settings.c"
                            "ui_setup.c" "ui_ir.c" "ui_learn.c" "ui_inputs.c" "ui_mem.c"
                            "ui_switcher.c" "ui_dash.c" "ui_status.c" "ui_themes.c" "ui_tv_setup.c" "ui_wifi.c" ${FONT_SRCS}
                       INCLUDE_DIRS "include"
                       REQUIRES hub
                       PRIV_INCLUDE_DIRS ".")

# LVGL calls the allocator in ui_mem.c but links after this component, so
# make the linker pull that object in up front.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")
