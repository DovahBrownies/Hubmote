# Weather module (README.md). Left out of the build entirely when
# CONFIG_HUB_MODULE_WEATHER is off. REQUIRES must not depend on the config
# (ESP-IDF reads requirements before it reads sdkconfig), so only the
# sources do. WHOLE_ARCHIVE keeps the self-registering module linked in.
if(CONFIG_HUB_MODULE_WEATHER)
    idf_component_register(SRCS "weather.c" "weather_data.c" "weather_ui.c"
                           REQUIRES hub ui json esp_timer
                           WHOLE_ARCHIVE)
else()
    idf_component_register(REQUIRES hub ui json esp_timer)
endif()
