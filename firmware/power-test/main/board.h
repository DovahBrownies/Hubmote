#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"

#define BOARD_LCD_H_RES 1024
#define BOARD_LCD_V_RES 600

/* Undo any deep-sleep pin holds left by board_lcd_pins_park(). Call first. */
void board_release_pin_holds(void);

/* I2C bus (GPIO8/9) and the CH32V003 IO expander. Leaves the backlight off. */
esp_err_t board_init_bus_and_expander(void);

/* GT911 reset through the expander, then the esp_lcd_touch driver. */
esp_err_t board_touch_init(esp_lcd_touch_handle_t *out);

/* RGB panel with one PSRAM framebuffer and a 10-line bounce buffer. */
esp_err_t board_lcd_init(uint32_t pclk_hz, esp_lcd_panel_handle_t *out);
esp_err_t board_lcd_set_pclk(uint32_t pclk_hz);
/* Stop scan-out and free the RGB peripheral. Releases its no-light-sleep
 * lock. LVGL must already be frozen; the panel cannot be used afterwards. */
void board_lcd_delete(void);

/* 0 = backlight driver disabled; 1..100 = PWM brightness. */
void board_backlight_set(uint8_t pct);
/* Panel bias rails (AVDD/VGH/VGL/VCOM boost) via LCD_VDD_EN. */
void board_panel_power(bool on);
void board_touch_sleep(void);

/* Battery voltage from the expander ADC, 0 when the read fails. */
uint16_t board_battery_mv(void);

/* Drive the non-strapping LCD pins low and hold them through deep sleep. */
void board_lcd_pins_park(void);
