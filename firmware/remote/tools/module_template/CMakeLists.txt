# __NAME__ module (README.md). Left out of the build when
# CONFIG_HUB_MODULE___ID_UPPER__ is off. REQUIRES must not depend on the
# config, so only the sources do. WHOLE_ARCHIVE keeps the self-registering
# module linked in.
if(CONFIG_HUB_MODULE___ID_UPPER__)
    idf_component_register(SRCS "__ID__.c"
                           REQUIRES hub ui
                           WHOLE_ARCHIVE)
else()
    idf_component_register(REQUIRES hub ui)
endif()
