# IR send / learn (RMT) and learned-remote storage (JSON files in a folder
# the app gives, on its filesystem). ir_codec.c is the pure logic; host_test/
# builds it on a PC.
idf_component_register(
    SRCS
        "ir.c"
        "ir_codec.c"
        "ir_store.c"
    INCLUDE_DIRS "include"
    # The public headers only need esp_err.h (esp_common, always present).
    PRIV_REQUIRES
        esp_driver_rmt
        esp_driver_gpio
        esp_timer
        json
        log
)
