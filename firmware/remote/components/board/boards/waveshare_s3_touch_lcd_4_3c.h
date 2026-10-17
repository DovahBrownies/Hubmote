#pragma once

/*
 * Waveshare ESP32-S3-Touch-LCD-4.3C: 4.3" 800x480 RGB panel, GT911 touch,
 * audio, and the 7B's kind of IO expander (a CH32V003 with Waveshare's
 * firmware: backlight PWM, battery ADC). Every GPIO is taken (I2S, RS485,
 * CAN), so there's no IR without changing the board. The code:
 * waveshare_s3_rgb.c and waveshare_ch32v003.c.
 *
 * Untested: written from Waveshare's schematic, ESP-IDF demos and board
 * support package (github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3C,
 * Waveshare-ESP32-components).
 */

#include "waveshare_s3_rgb.h"

#define BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-4.3C"
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

#define BOARD_BACKLIGHT_DIMMABLE 1 /* inverted PWM into the boost's feedback, as on the 7B */
#define BOARD_BATTERY_READING    1 /* the cell through a 3:1 divider to the expander's ADC */

/* The expander's pins (EXIO0..7). */
#define BOARD_EXIO_TOUCH_RST  1
#define BOARD_EXIO_DISP       2 /* backlight driver enable */
#define BOARD_EXIO_PA_CTRL    3 /* the speaker amplifier */
#define BOARD_EXIO_SD_CS      4
/* At start: as Waveshare's demos, everything high but the speaker
 * amplifier (EXIO0 and EXIO5 are the isolated inputs DI0 and DI1, EXIO6
 * and EXIO7 the outputs DO0 and DO1). */
#define BOARD_EXIO_START (0xFF & ~BIT(BOARD_EXIO_PA_CTRL))
