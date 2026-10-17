#pragma once

/*
 * Waveshare ESP32-S3-Touch-LCD-4.3: 4.3" 800x480 RGB panel (ST7262), GT911
 * touch, a CH422G IO expander. Its backlight is only on or off and the
 * battery can't be read. One GPIO is free: GPIO6 on the 3-pin "Sensor AD"
 * connector J6 (3V3, GND, GPIO6), which the IR LED gets; learning would
 * need a second pin. The code: waveshare_s3_rgb.c and waveshare_ch422g.c.
 *
 * Untested: written from Waveshare's schematic and ESP-IDF demos
 * (github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3).
 */

#include "waveshare_s3_rgb.h"

#define BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-4.3"
#define BOARD_UNTESTED    1
#define BOARD_LCD_H_RES   800
#define BOARD_LCD_V_RES   480
/* Waveshare's demos run it at 16 MHz; 14 MHz keeps the PSRAM traffic
 * where the 7B proved stable with this firmware's Wi-Fi and TLS load. */
#define BOARD_LCD_PCLK_HZ (14 * 1000 * 1000)
#define BOARD_LCD_HSYNC_PULSE 4
#define BOARD_LCD_HSYNC_BACK  8
#define BOARD_LCD_HSYNC_FRONT 8
#define BOARD_LCD_VSYNC_PULSE 4
#define BOARD_LCD_VSYNC_BACK  8
#define BOARD_LCD_VSYNC_FRONT 8

#define BOARD_BACKLIGHT_DIMMABLE 0
#define BOARD_BATTERY_READING    0

/* The expander's pins (EXIO0..7). */
#define BOARD_EXIO_TOUCH_RST  1
#define BOARD_EXIO_DISP       2 /* backlight driver enable */
#define BOARD_EXIO_LCD_RST    3
#define BOARD_EXIO_SD_CS      4
#define BOARD_EXIO_USB_SEL    5 /* 0 = native USB on GPIO19/20, 1 = CAN transceiver */
/* At start: resets released, SD card deselected, the native USB port
 * selected. */
#define BOARD_EXIO_START (BIT(BOARD_EXIO_TOUCH_RST) | BIT(BOARD_EXIO_LCD_RST) | BIT(BOARD_EXIO_SD_CS))
