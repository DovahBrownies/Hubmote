#pragma once

/*
 * Waveshare ESP32-S3-Touch-LCD-5: 5" 800x480 RGB panel, GT911 touch, a
 * CH422G IO expander; the same PCB (Rev1.2) as the 5B with the smaller
 * panel. Its backlight is only on or off (the dimming input is a test pad)
 * and the battery can't be read. Every GPIO is taken (RS485, CAN, the
 * RTC's interrupt), so there's no IR without changing the board. The code:
 * waveshare_s3_rgb.c and waveshare_ch422g.c.
 *
 * Untested: written from Waveshare's schematic and ESP-IDF demos
 * (github.com/waveshareteam/ESP32-S3-Touch-LCD-5). Waveshare's Arduino
 * configuration has vertical porches of 16 instead of 8: try that if the
 * picture rolls.
 */

#include "waveshare_s3_rgb.h"

#define BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-5"
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

/* The expander's pins (EXIO0..7). EXIO0 and EXIO5 are the isolated inputs
 * DI0 and DI1, but the CH422G's pins are all outputs or all inputs, so they
 * are driven low, as in Waveshare's demos. */
#define BOARD_EXIO_TOUCH_RST  1
#define BOARD_EXIO_DISP       2 /* backlight driver enable */
#define BOARD_EXIO_LCD_RST    3
#define BOARD_EXIO_SD_CS      4
/* At start: resets released, SD card deselected. */
#define BOARD_EXIO_START (BIT(BOARD_EXIO_TOUCH_RST) | BIT(BOARD_EXIO_LCD_RST) | BIT(BOARD_EXIO_SD_CS))
