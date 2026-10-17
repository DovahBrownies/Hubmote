#pragma once

/*
 * What Waveshare's ESP32-S3 boards with a landscape RGB panel share: the
 * 4.3, 4.3B, 4.3C, 5, 5B, 7 and 7B (not the 7C, whose I2C bus and two data
 * pins differ). The panel's pins, the I2C bus of the touch controller and
 * the IO expander, the GT911's interrupt and the BOOT button are the same
 * on all of them (Waveshare's schematics and demos,
 * waveshare_rgb_lcd_port.h). The code they share is waveshare_s3_rgb.c;
 * the expander's side is waveshare_ch422g.c or waveshare_ch32v003.c.
 *
 * A board's header includes this one and adds its panel (resolution,
 * clock, porches) and its expander's pins (BOARD_EXIO_*).
 */

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "soc/gpio_num.h"

#define WS_PIN_I2C_SDA   GPIO_NUM_8
#define WS_PIN_I2C_SCL   GPIO_NUM_9
#define WS_PIN_TOUCH_INT GPIO_NUM_4 /* RTC-capable, so touch wakes from deep sleep */
#define WS_PIN_BOOT      GPIO_NUM_0 /* BOOT button (to GND, 10k pull-up); also LCD G3 */

#define WS_PIN_LCD_HSYNC 46
#define WS_PIN_LCD_VSYNC 3
#define WS_PIN_LCD_DE    5
#define WS_PIN_LCD_PCLK  7
/* B3..B7, G2..G7, R3..R7 (RGB565; the panel's low bits are tied to GND). */
#define WS_LCD_DATA_PINS {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40}

/* From waveshare_s3_rgb.c to the expander's file: */

/* The I2C bus (made on the first call). */
esp_err_t ws_i2c_bus(i2c_master_bus_handle_t *out);

/* From the expander's file to waveshare_s3_rgb.c: */

/* Drives the expander pins in mask (BIT(BOARD_EXIO_...)) high or low. */
void ws_exio_set(uint8_t mask, bool high);
/* The panel's bias rails (LCD_VDD_EN) where the board switches them. */
void ws_panel_power(bool on);
