# Small NVS helpers: one value in one namespace per call.
idf_component_register(
    SRCS "kv.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES nvs_flash log
)
