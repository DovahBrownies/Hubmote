#pragma once

/*
 * Waveshare ESP32-S3-Touch-LCD-5B: 5" 1024x600 RGB panel, GT911 touch, a
 * CH422G IO expander; the same PCB (Rev1.2) as the 5 with the larger
 * panel. Its backlight is only on or off (the dimming input is a test pad)
 * and the battery can't be read. Every GPIO is taken (RS485, CAN, the
 * RTC's interrupt), so there's no IR without changing the board. The code:
 * waveshare_s3_rgb.c and waveshare_ch422g.c.
 *
 * Untested: written from Waveshare's schematic and ESP-IDF demos
 * (github.com/waveshareteam/ESP32-S3-Touch-LCD-5).
 */

#include "waveshare_s3_rgb.h"

#define BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-5B"
#define BOARD_UNTESTED    1
#define BOARD_LCD_H_RES   1024
#define BOARD_LCD_V_RES   600
/* Waveshare's demos run it at 21 MHz; the 7B, with the same timings,
 * slipped above 14 MHz under this firmware's load. */
#define BOARD_LCD_PCLK_HZ (14 * 1000 * 1000)
#define BOARD_LCD_HSYNC_PULSE 30
#define BOARD_LCD_HSYNC_BACK  145
#define BOARD_LCD_HSYNC_FRONT 170
#define BOARD_LCD_VSYNC_PULSE 2
#define BOARD_LCD_VSYNC_BACK  23
#define BOARD_LCD_VSYNC_FRONT 12

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
