#pragma once

/*
 * Waveshare ESP32-S3-Touch-LCD-7 (schematic V1.2): 7" 800x480 RGB panel
 * (ST7262), GT911 touch, a CH422G IO expander. The 7B's twin (the same nets
 * line for line: J8 with GPIO6, the H3 UART header behind SW1, the USB
 * switch on EXIO5), but its backlight is only on or off and the battery
 * can't be read. Its module is an ESP32-S3-WROOM-1-N8R8 in the schematic
 * and Waveshare's demos (8 MB flash; some listings say 16 MB), hence
 * waveshare_s3_touch_lcd_7.defaults. The code: waveshare_s3_rgb.c and
 * waveshare_ch422g.c.
 *
 * Untested: written from Waveshare's schematic and ESP-IDF demos
 * (github.com/waveshareteam/ESP32-S3-Touch-LCD-7).
 */

#include "waveshare_s3_rgb.h"

#define BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-7"
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

#define BOARD_BACKLIGHT_DIMMABLE 0 /* DISP drives the boost's enable; its dimming input isn't fitted */
#define BOARD_BATTERY_READING    0 /* the cell's divider only reaches test pad TP1 */

/* The expander's pins (EXIO0..7). */
#define BOARD_EXIO_TOUCH_RST  1
#define BOARD_EXIO_DISP       2 /* backlight driver enable */
#define BOARD_EXIO_LCD_RST    3
#define BOARD_EXIO_SD_CS      4
#define BOARD_EXIO_USB_SEL    5 /* 0 = native USB on GPIO19/20, 1 = CAN transceiver */
#define BOARD_EXIO_LCD_VDD_EN 6 /* the panel's bias boost (AP3012 SHDN) */
/* At start: resets released, SD card deselected, the panel's bias on, the
 * native USB port selected. Waveshare's ESP-IDF demos leave EXIO6 low; the
 * schematic has it enable the bias boost (as on the 7B), so it goes high. */
#define BOARD_EXIO_START \
    (BIT(BOARD_EXIO_TOUCH_RST) | BIT(BOARD_EXIO_LCD_RST) | BIT(BOARD_EXIO_SD_CS) | BIT(BOARD_EXIO_LCD_VDD_EN))
