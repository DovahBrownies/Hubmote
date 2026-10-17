# Board support: each board's header and defaults in boards/, its code there
# too, shared by boards with the same chips; picked with -D HM_BOARD=<id>
# (the HM_BOARD choice in menuconfig). REQUIRES must not depend on the choice
# (ESP-IDF reads requirements before sdkconfig), so every board's
# requirements are listed; only the chosen board's sources are built.
# Adding a board: docs/porting.md.
set(srcs "")
if(CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_7B OR CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_4_3C)
    list(APPEND srcs "boards/waveshare_s3_rgb.c" "boards/waveshare_ch32v003.c")
elseif(CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_7 OR CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_5B
       OR CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_5 OR CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_4_3
       OR CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_4_3B)
    list(APPEND srcs "boards/waveshare_s3_rgb.c" "boards/waveshare_ch422g.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include" "boards"
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_lcd esp_timer)
