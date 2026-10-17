cmake_minimum_required(VERSION 3.16)
# Modules: every folder in modules/ is a component (docs/modules.md).
# tools/new_module.py makes new ones from tools/module_template.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/modules")

# The board (docs/porting.md): -D HM_BOARD=<id> adds its defaults,
# components/board/boards/<id>.defaults, to sdkconfig.defaults (and
# sdkconfig.release). Defaults only fill a new sdkconfig, so one made for
# another board is refused rather than silently kept. Without HM_BOARD the
# board is sdkconfig's (menuconfig > Hubmote > Board; the 7B by default).
set(HM_BOARD "" CACHE STRING "board id (components/board/boards/<id>.defaults)")
if(HM_BOARD)
    set(board_defaults "${CMAKE_CURRENT_LIST_DIR}/components/board/boards/${HM_BOARD}.defaults")
    if(NOT EXISTS "${board_defaults}")
        message(FATAL_ERROR "HM_BOARD: no board '${HM_BOARD}' (components/board/boards/*.defaults)")
    endif()
    if(NOT SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
    endif()
    list(APPEND SDKCONFIG_DEFAULTS "${board_defaults}")
    set(existing "${SDKCONFIG}")
    if(NOT existing)
        set(existing "sdkconfig")
    endif()
    get_filename_component(existing "${existing}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
    string(TOUPPER "${HM_BOARD}" board_option)
    set(board_option "CONFIG_HM_BOARD_${board_option}=y")
    if(EXISTS "${existing}")
        file(STRINGS "${existing}" chosen REGEX "^CONFIG_HM_BOARD_[A-Z0-9_]+=y$")
        if(chosen AND NOT "${board_option}" IN_LIST chosen)
            message(FATAL_ERROR "${existing} is for another board (${chosen}): "
                                "move it away, or build ${HM_BOARD} with its own -B and -D SDKCONFIG")
        endif()
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# cJSON recurses once per nesting level (about 64 bytes of stack) and allows
# 1000 levels, more than any task's stack: a few bytes of "[[[[" from a
# device posing as the companion would overflow it. Nothing the remote reads
# nests deeper than a few levels.
idf_build_set_property(COMPILE_DEFINITIONS "CJSON_NESTING_LIMIT=16" APPEND)
project(hubmote)
