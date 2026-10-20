/*
 * Hubmote for the Waveshare ESP32-S3-Touch-LCD-7B: a touchscreen remote for
 * a Google TV Streamer (Android TV Remote protocol v2 over Wi-Fi), a TV and
 * other devices over IR, with modules on a Modules tab. This starts the board,
 * the display and the controller (controller.c), which runs everything else.
 */
#include <stdlib.h>
#include "board.h"
#include "cJSON.h"
#include "controller.h"
#include "esp_heap_caps.h"
#include "devtools.h"
#include "display.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "net.h"
#include "netlog.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "main";

/* cJSON makes a node per value and copies every string: web answers, the
 * companion's app list and learned remotes with long RAW codes would take
 * tens of KB of internal RAM. Set before any task parses. */
static void *json_malloc(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
}

/* Why the remote last restarted, for the log: a crash and an update look
 * alike to someone holding it. */
static void log_reset_reason(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    switch (r) {
    case ESP_RST_PANIC:
        ESP_LOGE(TAG, "restarted after a crash (panic)");
        break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        ESP_LOGE(TAG, "restarted after a crash (watchdog %d)", (int)r);
        break;
    case ESP_RST_BROWNOUT:
        ESP_LOGW(TAG, "restarted after a brownout (low battery or supply)");
        break;
    case ESP_RST_SW:
        ESP_LOGI(TAG, "restarted by the firmware (an update, or Settings)");
        break;
    case ESP_RST_DEEPSLEEP:
        break; /* woken: said below */
    default:
        ESP_LOGI(TAG, "started (reset reason %d)", (int)r);
        break;
    }
}

void app_main(void)
{
    netlog_init(); /* from here on, log lines are kept for the Wi-Fi log */
    log_reset_reason();
    cJSON_Hooks hooks = {.malloc_fn = json_malloc, .free_fn = free};
    cJSON_InitHooks(&hooks);
    esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
    if (wake == ESP_SLEEP_WAKEUP_EXT0 || wake == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "turned on by %s", wake == ESP_SLEEP_WAKEUP_EXT0 ? "BOOT" : "touch");
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(board_init());
    bool turned = display_turned_setting(); /* Settings > Screen > Rotate 180° */
    esp_lcd_touch_handle_t touch = NULL;
    if (board_touch_init(turned, &touch) != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed; the screen will not respond");
        touch = NULL;
    }

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 12288;
    lvgl_cfg.task_affinity = 1; /* Wi-Fi and TLS run on core 0 */
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* Two framebuffers, swapped between frames, so screen changes never
     * tear; turned 180 degrees if set (main/display.c). */
    lv_display_t *disp = display_start(touch, turned);
    devtools_init();

    net_init();
    controller_start();

    /* Backlight only after the first frame, so boot never flashes garbage. */
    if (lvgl_port_lock(0)) {
        lv_refr_now(disp);
        lvgl_port_unlock();
    }
    board_backlight_set((uint8_t)controller_brightness());
    controller_log_heap("started");
}
