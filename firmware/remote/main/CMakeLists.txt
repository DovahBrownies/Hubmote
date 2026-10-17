idf_component_register(SRCS "main.c" "battery.c" "build_info.c" "devtools.c" "display.c" "net.c" "netlog.c" "ota.c" "controller.c" "ctrl_companion.c" "ctrl_screen.c" "ctrl_setup.c" "ir_ctl.c" "storage.c" "tv_ir.c" "tv_codes.c" "update.c"
                       INCLUDE_DIRS "."
                       REQUIRES board ui hub atv_remote ir companion kv esp_wifi esp_netif esp_event nvs_flash esp_timer esp_lcd
                                esp_http_server app_update esp_http_client mbedtls json bootloader_support)

# Wi-Fi credentials come from firmware/remote/secrets.env (gitignored; see
# secrets.env.example). They go into a header generated in the build
# directory, never into sdkconfig. Editing secrets.env re-runs CMake on the
# next build, so new values are picked up without a reconfigure.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # HM_NO_SECRETS (scripts/hm-web.ps1): a build for others, without this
    # machine's Wi-Fi or update token. The remote asks for Wi-Fi and makes
    # its own token.
    set(secrets_file "${PROJECT_DIR}/secrets.env")
    set(secrets_text "")
    if(HM_NO_SECRETS)
        message(STATUS "HM_NO_SECRETS: building without secrets.env")
    elseif(EXISTS "${secrets_file}")
        file(READ "${secrets_file}" secrets_text)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${secrets_file}")
    else()
        message(STATUS "No secrets.env: building without Wi-Fi credentials")
    endif()

    # KEY=value, one per line; "#" starts a comment line. Wrap a value in
    # double quotes to keep leading or trailing spaces.
    function(hm_secret key out)
        set(value "")
        set(text "\n${secrets_text}")
        if(text MATCHES "\n[ \t]*${key}[ \t]*=([^\r\n]*)")
            string(STRIP "${CMAKE_MATCH_1}" value)
            if(value MATCHES "^\"(.*)\"$")
                set(value "${CMAKE_MATCH_1}")
            endif()
        endif()
        string(REPLACE "\\" "\\\\" value "${value}")
        string(REPLACE "\"" "\\\"" value "${value}")
        set(${out} "${value}" PARENT_SCOPE)
    endfunction()

    # Only Wi-Fi: OTA_TOKEN is for the PC's tools, and the remote makes its
    # own token (main/ota.c), so no image carries it.
    hm_secret(WIFI_SSID wifi_ssid)
    hm_secret(WIFI_PASSWORD wifi_password)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/hm_secrets.h")
    file(WRITE "${header}.tmp"
         "/* Generated from secrets.env by main/CMakeLists.txt. Do not commit. */\n"
         "#pragma once\n"
         "#define HM_WIFI_SSID \"${wifi_ssid}\"\n"
         "#define HM_WIFI_PASSWORD \"${wifi_password}\"\n")
    file(COPY_FILE "${header}.tmp" "${header}" ONLY_IF_DIFFERENT)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

    # The build time shown in Settings and by /version (build_info.c). It
    # is stamped on every build: ESP-IDF's own date only changes when
    # ESP-IDF happens to recompile esp_app_desc.c.
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/build_stamp.h")
    add_custom_target(hm_build_stamp
        COMMAND ${CMAKE_COMMAND} -DOUT=${stamp} -P "${CMAKE_CURRENT_LIST_DIR}/build_stamp.cmake"
        BYPRODUCTS "${stamp}"
        VERBATIM)
    add_dependencies(${COMPONENT_LIB} hm_build_stamp)
endif()
