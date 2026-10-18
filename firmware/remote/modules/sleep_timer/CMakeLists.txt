# Sleep timer module (README.md). Left out of the build when
# CONFIG_HUB_MODULE_SLEEP_TIMER is off. REQUIRES must not depend on the
# config, so only the sources do. WHOLE_ARCHIVE keeps the self-registering
# module linked in.
if(CONFIG_HUB_MODULE_SLEEP_TIMER)
    idf_component_register(SRCS "sleep_timer.c"
                           REQUIRES hub ui esp_timer
                           WHOLE_ARCHIVE)
else()
    idf_component_register(REQUIRES hub ui esp_timer)
endif()
