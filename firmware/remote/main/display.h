#pragma once

/*
 * The LVGL display on the RGB panel, optionally turned 180 degrees (for the
 * enclosure), with touch turned to match. The choice is Settings > Screen >
 * Rotate 180°, kept in NVS (ctrl/turn); HM_DISPLAY_ROTATE_180 is only the
 * default for a remote's first start.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_touch.h"
#include "lvgl.h"

/* Whether the screen is turned, as saved. On a remote's first start this
 * is the menuconfig default, which is then saved, so later builds with
 * another default don't turn it. Call after nvs_flash_init(). */
bool display_turned_setting(void);

/* Creates the panel and the LVGL display, turned or not, and attaches
 * touch (may be NULL). Call after lvgl_port_init(). */
lv_display_t *display_start(esp_lcd_touch_handle_t touch, bool turned);

/* Turns the screen and touch from the next frame, and saves it. */
void display_set_turned(bool turned);
bool display_is_turned(void);

/* Copies the frame the panel is showing (BOARD_LCD_H_RES x BOARD_LCD_V_RES
 * RGB565, upright as LVGL drew it) into dst. Takes the LVGL lock for the copy. */
bool display_capture(uint16_t *dst);
