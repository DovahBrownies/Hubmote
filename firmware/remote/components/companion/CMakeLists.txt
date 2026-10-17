# Client for the Streamer companion app (HTTP on port 6470 of the TV).
idf_component_register(
    SRCS "companion.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES json lwip log esp_timer nvs_flash esp_rom
)
