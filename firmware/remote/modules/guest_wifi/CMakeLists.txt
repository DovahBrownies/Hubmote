# Guest Wi-Fi module (README.md). Left out of the build when
# CONFIG_HUB_MODULE_GUEST_WIFI is off. REQUIRES must not depend on the
# config, so only the sources do. WHOLE_ARCHIVE keeps the self-registering
# module linked in.
if(CONFIG_HUB_MODULE_GUEST_WIFI)
    idf_component_register(SRCS "guest_wifi.c"
                           REQUIRES hub ui
                           WHOLE_ARCHIVE)
else()
    idf_component_register(REQUIRES hub ui)
endif()
