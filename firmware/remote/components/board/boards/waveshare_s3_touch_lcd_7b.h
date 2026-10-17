#pragma once

/*
 * Waveshare ESP32-S3-Touch-LCD-7B: 7" 1024x600 RGB panel (ST7262), GT911
 * touch, a CH32V003 IO expander (backlight PWM, resets, panel power,
 * battery ADC). The code: waveshare_s3_rgb.c and waveshare_ch32v003.c.
 * The board Hubmote is developed on.
 */

#include "waveshare_s3_rgb.h"

#define BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-7B"
#define BOARD_LCD_H_RES   1024
#define BOARD_LCD_V_RES   600
/* Bench-tested: 14 MHz is stable; 18 MHz brings sync slips back and
 * 20 MHz breaks GT911 touch. */
#define BOARD_LCD_PCLK_HZ (14 * 1000 * 1000)
#define BOARD_LCD_HSYNC_PULSE 30
#define BOARD_LCD_HSYNC_BACK  145
#define BOARD_LCD_HSYNC_FRONT 170
#define BOARD_LCD_VSYNC_PULSE 2
#define BOARD_LCD_VSYNC_BACK  23
#define BOARD_LCD_VSYNC_FRONT 12

#define BOARD_BACKLIGHT_DIMMABLE 1
#define BOARD_BATTERY_READING    1

/* The expander's pins (EXIO0..7). */
#define BOARD_EXIO_TOUCH_RST  1
#define BOARD_EXIO_DISP       2 /* backlight driver enable */
#define BOARD_EXIO_LCD_RST    3
#define BOARD_EXIO_SD_CS      4
#define BOARD_EXIO_USB_SEL    5 /* 0 = native USB on GPIO19/20, 1 = CAN transceiver */
#define BOARD_EXIO_LCD_VDD_EN 6 /* panel bias boost */
/* At start: everything high (Waveshare's default; EXIO0 and EXIO7 too, as
 * in Waveshare's demos) except USB_SEL, low so the native USB-C port works
 * instead of routing GPIO19/20 to the CAN transceiver. */
#define BOARD_EXIO_START (0xFF & ~BIT(BOARD_EXIO_USB_SEL))
