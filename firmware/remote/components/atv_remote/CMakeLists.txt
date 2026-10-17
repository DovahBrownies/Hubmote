idf_component_register(
    SRCS
        "src/atv_remote.c"
        "src/atv_pb.c"
        "src/atv_msg.c"
        "src/atv_identity.c"
        "src/atv_secret.c"
        "src/atv_discover.c"
        "src/atv_selftest.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    # espressif/mdns comes from idf_component.yml (added as a private requirement).
    PRIV_REQUIRES mbedtls nvs_flash esp_timer esp_netif lwip log
)
