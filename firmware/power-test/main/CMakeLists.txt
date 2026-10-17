idf_component_register(SRCS "main.c" "board.c" "net.c" "runner.c" "ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio esp_wifi esp_netif
                                esp_pm esp_timer nvs_flash lwip)
