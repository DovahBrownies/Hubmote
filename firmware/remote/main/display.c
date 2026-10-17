/*
 * The LVGL display on the RGB panel, optionally turned 180 degrees.
 *
 * LVGL draws in direct mode into two PSRAM framebuffers owned here, so
 * screen changes never tear: the panel shows one while LVGL draws into the
 * other, and they swap between frames. The panel driver keeps no
 * framebuffer of its own. It asks for each 10-line bounce buffer, and
 * fill_bounce() copies those lines out of the shown framebuffer.
 *
 * Turning a picture 180 degrees reverses its pixel stream: the panel's
 * pixel p is framebuffer pixel N-1-p. So the turned copy just reads the
 * framebuffer backwards, which costs about as much as the plain copy and
 * leaves LVGL and its dirty-area tracking untouched. Touch is turned to
 * match (board_touch_init(), then the touch driver's mirror flags), so the
 * setting applies at once, without a restart.
 */
#include "display.h"

#include <string.h>
#include "board.h"
#include "controller.h"
#include "esp32s3/rom/cache.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "ui_kit.h"

/* The board's panel and the screen the UI is laid out for (menuconfig >
 * Hubmote > Board sets both). */
_Static_assert(BOARD_LCD_H_RES == UI_W && BOARD_LCD_V_RES == UI_H, "CONFIG_HM_SCREEN_W/H don't match the board's panel");

static const char *TAG = "display";

#define FB_PX    (BOARD_LCD_H_RES * BOARD_LCD_V_RES)
#define SWAP_MS  200 /* a frame takes ~62 ms at 14 MHz */
#define NVS_TURN "turn"
#ifdef CONFIG_HM_DISPLAY_ROTATE_180
#define TURN_DEFAULT 1
#else
#define TURN_DEFAULT 0
#endif

_Static_assert(FB_PX % 2 == 0, "the turned copy moves pixels in pairs");

static uint16_t *s_fb[2];
static volatile uint8_t s_shown;    /* framebuffer the panel is showing */
static volatile int8_t s_next = -1; /* framebuffer to show from the next frame */
static SemaphoreHandle_t s_swapped;
static volatile bool s_turned; /* read for every bounce buffer */
static esp_lcd_touch_handle_t s_touch;

/* Bounce buffers hold whole lines and frames start at pixel 0, so pos_px
 * and len_bytes / 2 are even and the word reads below stay aligned. */
static IRAM_ATTR bool fill_bounce(esp_lcd_panel_handle_t panel, void *bounce, int pos_px, int len_bytes, void *ctx)
{
    const uint16_t *fb = s_fb[s_shown];
    int len_px = len_bytes / 2;
    int next_px = pos_px + len_px;
    if (!s_turned) {
        memcpy(bounce, fb + pos_px, len_bytes);
        if (next_px < FB_PX) {
            Cache_Start_DCache_Preload((uint32_t)(fb + next_px), len_bytes, 0);
        }
    } else {
        /* Read pixel pairs backwards from N - pos_px; swap each pair. */
        const uint32_t *src = (const uint32_t *)(fb + FB_PX - pos_px);
        uint32_t *dst = bounce;
        for (int n = len_px / 2; n > 0; n--) {
            uint32_t v = *--src;
            *dst++ = (v >> 16) | (v << 16);
        }
        if (next_px < FB_PX) {
            Cache_Start_DCache_Preload((uint32_t)(fb + FB_PX - next_px - len_px), len_bytes, 1);
        }
    }
    return false;
}

/* The last bounce buffer of a frame is filled: swap here, so the next frame
 * comes whole from the new framebuffer. */
static IRAM_ATTR bool frame_done(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *ed, void *ctx)
{
    BaseType_t woken = pdFALSE;
    int8_t next = s_next;
    if (next >= 0) {
        s_shown = (uint8_t)next;
        s_next = -1;
        xSemaphoreGiveFromISR(s_swapped, &woken);
    }
    return woken == pdTRUE;
}

/* Direct mode: LVGL has drawn the frame's changes into px_map. Show it, and
 * hand control back only once the panel has left the other framebuffer,
 * which LVGL draws into next. */
static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (lv_display_flush_is_last(disp)) {
        xSemaphoreTake(s_swapped, 0);
        s_next = px_map == (uint8_t *)s_fb[1] ? 1 : 0;
        if (xSemaphoreTake(s_swapped, pdMS_TO_TICKS(SWAP_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "frame swap timed out");
        }
    }
    lv_display_flush_ready(disp);
}

bool display_capture(uint16_t *dst)
{
    if (!s_fb[0] || !lvgl_port_lock(1000)) {
        return false;
    }
    /* With the lock held LVGL can't draw or swap, so the shown frame is whole. */
    memcpy(dst, s_fb[s_shown], FB_PX * sizeof(uint16_t));
    lvgl_port_unlock();
    return true;
}

bool display_turned_setting(void)
{
    uint8_t turned = TURN_DEFAULT;
    nvs_handle_t h;
    if (nvs_open(CTRL_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_get_u8(h, NVS_TURN, &turned) != ESP_OK) {
            turned = TURN_DEFAULT;
            if (nvs_set_u8(h, NVS_TURN, turned) == ESP_OK) {
                nvs_commit(h);
            }
        }
        nvs_close(h);
    }
    return turned != 0;
}

void display_set_turned(bool turned)
{
    s_turned = turned;
    if (s_touch) {
        esp_lcd_touch_set_mirror_x(s_touch, turned);
        esp_lcd_touch_set_mirror_y(s_touch, turned);
    }
    nvs_handle_t h;
    if (nvs_open(CTRL_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_u8(h, NVS_TURN, turned) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "%s", turned ? "turned 180 degrees" : "upright");
}

bool display_is_turned(void)
{
    return s_turned;
}

lv_display_t *display_start(esp_lcd_touch_handle_t touch, bool turned)
{
    s_turned = turned;
    s_touch = touch;
    for (int i = 0; i < 2; i++) {
        s_fb[i] = heap_caps_aligned_calloc(64, FB_PX, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        ESP_ERROR_CHECK(s_fb[i] ? ESP_OK : ESP_ERR_NO_MEM);
    }
    s_swapped = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_swapped ? ESP_OK : ESP_ERR_NO_MEM);

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_bounce_empty = fill_bounce,
        .on_frame_buf_complete = frame_done,
    };
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(board_lcd_init(BOARD_LCD_PCLK_HZ, &cbs, NULL, &panel));

    lv_display_t *disp = NULL;
    if (lvgl_port_lock(0)) {
        disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
        lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(disp, s_fb[0], s_fb[1], FB_PX * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(disp, flush);
        lvgl_port_unlock();
    }
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);
    if (touch) {
        lvgl_port_touch_cfg_t touch_cfg = {.disp = disp, .handle = touch};
        lvgl_port_add_touch(&touch_cfg);
    }
    ESP_LOGI(TAG, "%dx%d, %s", BOARD_LCD_H_RES, BOARD_LCD_V_RES, s_turned ? "turned 180 degrees" : "upright");
    return disp;
}
