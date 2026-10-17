#pragma once

/*
 * What the firmware needs from a board: start-up, touch, the RGB panel, the
 * backlight, deep sleep and the battery voltage. Each board describes itself
 * in boards/<id>.h and is built from the code there (boards that share
 * chips share code: waveshare_s3_rgb.c with waveshare_ch422g.c or
 * waveshare_ch32v003.c); -D HM_BOARD=<id>, with boards/<id>.defaults, picks
 * one (menuconfig > Hubmote > Board shows it). Adding a board: docs/porting.md.
 *
 * Every board's header defines:
 *   BOARD_NAME                the product's name
 *   BOARD_LCD_H_RES, _V_RES   the panel's resolution (= CONFIG_HM_SCREEN_W/H)
 *   BOARD_LCD_PCLK_HZ         its pixel clock
 *   BOARD_BACKLIGHT_DIMMABLE  1 if board_backlight_set() dims, 0 if it only
 *                             switches the backlight on and off
 *   BOARD_BATTERY_READING     1 if board_battery_mv() reads the cell
 *   BOARD_UNTESTED            1 until someone has run Hubmote on the board
 *                             (the installer site says so)
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "sdkconfig.h"

#if CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_7B
#include "waveshare_s3_touch_lcd_7b.h"
#elif CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_7
#include "waveshare_s3_touch_lcd_7.h"
#elif CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_5B
#include "waveshare_s3_touch_lcd_5b.h"
#elif CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_5
#include "waveshare_s3_touch_lcd_5.h"
#elif CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_4_3
#include "waveshare_s3_touch_lcd_4_3.h"
#elif CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_4_3B
#include "waveshare_s3_touch_lcd_4_3b.h"
#elif CONFIG_HM_BOARD_WAVESHARE_S3_TOUCH_LCD_4_3C
#include "waveshare_s3_touch_lcd_4_3c.h"
#else
#error "No board chosen: menuconfig > Hubmote > Board (docs/porting.md)"
#endif

/* The board's buses and chips (on Waveshare's boards: I2C and the IO
 * expander). Leaves the backlight off. Call first. */
esp_err_t board_init(void);

/* The touch controller, through esp_lcd_touch (on Waveshare's boards: GT911,
 * reset through the expander). turned: report touches turned 180 degrees,
 * to match a turned picture. */
esp_err_t board_touch_init(bool turned, esp_lcd_touch_handle_t *out);

/* RGB panel with a 10-line bounce buffer and no framebuffer of its own: it
 * calls cbs->on_bounce_empty to fill each bounce buffer from the caller's
 * framebuffers (main/display.c, which can then turn the picture). cbs is
 * registered before the panel starts. */
esp_err_t board_lcd_init(uint32_t pclk_hz, const esp_lcd_rgb_panel_event_callbacks_t *cbs, void *cb_ctx,
                         esp_lcd_panel_handle_t *out);

/* 0 = backlight driver disabled; 1..100 = PWM brightness (or simply on,
 * where BOARD_BACKLIGHT_DIMMABLE is 0). */
void board_backlight_set(uint8_t pct);
uint8_t board_backlight_get(void);
/* When the backlight level last changed (esp_timer time, 0 if never). */
int64_t board_backlight_changed_us(void);
/* Called about once a second. On the 7B it re-sends the backlight and
 * output state, as the expander occasionally drops a write; boards without
 * that problem can do nothing. */
void board_backlight_refresh(void);

/* "Off": backlight and panel off, then deep sleep until the screen is
 * touched (or a button pressed). Waking restarts the firmware. Doesn't
 * return. */
void board_deep_sleep(void);

/* Battery voltage in mV (on the 7B from the expander's ADC), 0 when there
 * is none, the board can't read it, or the read fails. */
uint16_t board_battery_mv(void);
