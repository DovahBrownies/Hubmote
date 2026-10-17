/*
 * Hubmote power test for the Waveshare ESP32-S3-Touch-LCD-7B.
 *
 * Puts the board into one known state at a time (backlight level, CPU
 * clock, pixel clock, render load, Wi-Fi mode, and the screen-off / sleep
 * states) so an external meter can read the current of each. See README.md.
 */
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "net.h"
#include "nvs_flash.h"
#include "runner.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    board_release_pin_holds();

    /* Wi-Fi keeps PHY calibration data in NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    runner_init();
    ESP_ERROR_CHECK(board_init_bus_and_expander());

    esp_lcd_touch_handle_t touch = NULL;
    if (board_touch_init(&touch) != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed; the menu will not respond (RESET still works)");
        touch = NULL;
    }

    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(board_lcd_init(runner_settings()->pclk_mhz * 1000000u, &panel));

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 8192;
    lvgl_cfg.task_affinity = 1; /* Wi-Fi runs on core 0 */
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* Partial rendering into a PSRAM draw buffer, copied into the single
     * framebuffer on flush. Tearing is irrelevant here; simplicity is not. */
    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_H_RES * 100,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_spiram = 1},
    };
    lvgl_port_display_rgb_cfg_t rgb_cfg = {.flags = {.bb_mode = 1}};
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (touch) {
        lvgl_port_touch_cfg_t touch_cfg = {.disp = disp, .handle = touch};
        lvgl_port_add_touch(&touch_cfg);
    }

    net_init();

    /* Draw the first frame before the backlight comes on, then apply the
     * settings (which switch the backlight on) and time the boot. */
    lvgl_port_lock(0);
    ui_create();
    lv_refr_now(disp);
    runner_apply(runner_settings());
    lvgl_port_unlock();
    runner_mark_first_frame();
    lvgl_port_lock(0);
    ui_set_boot_note(runner_boot_note());
    lvgl_port_unlock();

    ESP_LOGI(TAG, "heap: internal %u KB free (largest %u KB), PSRAM %u KB free",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    runner_start();
}
