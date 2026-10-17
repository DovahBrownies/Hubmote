/*
 * Runs the measurement states.
 *
 * Screen-on states are just settings (runner_apply). Screen-off states
 * freeze LVGL, tear down what the state says, dwell, then reboot, because
 * bringing the RGB panel back is more code than this harness needs. State
 * that has to survive those reboots (settings, sequence position, what just
 * ran) lives in RTC_NOINIT memory and is discarded on any power-on, EN-pin
 * or crash reset, so RESET always aborts back to a clean menu.
 */
#include "runner.h"

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "ui.h"

static const char *TAG = "runner";

#define PT_MAGIC 0x50574554u /* "PWET" */

typedef struct {
    uint32_t magic;
    pt_settings_t settings;
    uint16_t duration_s;
    int8_t seq_next;   /* sequence step to resume after a reboot, -1 = none */
    uint8_t last_test; /* screen-off test that ended in the reboot just done */
    uint16_t last_test_s;
} pt_rtc_t;
RTC_NOINIT_ATTR static pt_rtc_t s_rtc;

static const pt_settings_t DEFAULTS = {
    .backlight_pct = 50, .cpu_mhz = 240, .pclk_mhz = 14, .animate = false, .wifi = NET_OFF,
};

typedef struct {
    const char *name;
    pt_settings_t settings;
    pt_test_t test;
} seq_step_t;

/* backlight %, CPU MHz, PCLK MHz, animate, Wi-Fi. Each screen-on step moves
 * one axis away from the 50% / 240 / 14 / static / off baseline. */
#define S(bl_, cpu_, pclk_, anim_, wifi_) \
    {.backlight_pct = bl_, .cpu_mhz = cpu_, .pclk_mhz = pclk_, .animate = anim_, .wifi = wifi_}
static const seq_step_t SEQ[] = {
    {"Backlight 100%",          S(100, 240, 14, false, NET_OFF),       PT_TEST_NONE},
    {"Backlight 75%",           S(75,  240, 14, false, NET_OFF),       PT_TEST_NONE},
    {"Backlight 50%",           S(50,  240, 14, false, NET_OFF),       PT_TEST_NONE},
    {"Backlight 25%",           S(25,  240, 14, false, NET_OFF),       PT_TEST_NONE},
    {"Backlight 10%",           S(10,  240, 14, false, NET_OFF),       PT_TEST_NONE},
    {"Backlight 1%",            S(1,   240, 14, false, NET_OFF),       PT_TEST_NONE},
    {"CPU 160 MHz",             S(50,  160, 14, false, NET_OFF),       PT_TEST_NONE},
    {"CPU 80 MHz",              S(50,  80,  14, false, NET_OFF),       PT_TEST_NONE},
    {"Pixel clock 7 MHz",       S(50,  240, 7,  false, NET_OFF),       PT_TEST_NONE},
    {"Full-screen animation",   S(50,  240, 14, true,  NET_OFF),       PT_TEST_NONE},
    {"Wi-Fi on, no power save", S(50,  240, 14, false, NET_ON_NO_PS),  PT_TEST_NONE},
    {"Wi-Fi on, modem sleep",   S(50,  240, 14, false, NET_ON_MIN_PS), PT_TEST_NONE},
    {NULL,                      S(50,  240, 14, false, NET_OFF),       PT_TEST_BACKLIGHT_OFF},
    {NULL,                      S(50,  240, 14, false, NET_OFF),       PT_TEST_PANEL_OFF},
    {NULL,                      S(50,  240, 14, false, NET_OFF),       PT_TEST_TOUCH_SLEEP},
    {NULL,                      S(50,  240, 14, false, NET_OFF),       PT_TEST_LIGHT_SLEEP},
    {NULL,                      S(50,  240, 14, false, NET_OFF),       PT_TEST_WARM_STANDBY},
    {NULL,                      S(50,  240, 14, false, NET_OFF),       PT_TEST_DEEP_SLEEP},
    {NULL,                      S(50,  240, 14, false, NET_OFF),       PT_TEST_DEEP_SLEEP_PARKED},
};
#define SEQ_LEN ((int)(sizeof(SEQ) / sizeof(SEQ[0])))

static const char *const TEST_NAMES[PT_TEST_COUNT] = {
    [PT_TEST_NONE] = "",
    [PT_TEST_BACKLIGHT_OFF] = "Backlight off, panel scanning",
    [PT_TEST_PANEL_OFF] = "Panel off, CPU idle",
    [PT_TEST_TOUCH_SLEEP] = "Panel off + touch asleep",
    [PT_TEST_LIGHT_SLEEP] = "Light sleep, radio off",
    [PT_TEST_WARM_STANDBY] = "Warm standby (Wi-Fi + keepalive)",
    [PT_TEST_DEEP_SLEEP] = "Deep sleep",
    [PT_TEST_DEEP_SLEEP_PARKED] = "Deep sleep, LCD pins parked",
    [PT_TEST_WAKE_LATENCY] = "Wake latency",
};

typedef struct {
    bool sequence;
    pt_test_t test;
} cmd_t;

static QueueHandle_t s_queue;
static volatile bool s_abort;
static volatile bool s_busy;
static pt_settings_t s_applied;
static bool s_applied_valid;
static esp_reset_reason_t s_reset_reason;
static pt_test_t s_finished_test;
static uint16_t s_finished_s;
static int s_resume_step = -1;
static char s_boot_note[256];

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power-on / RESET button";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT (battery sagged)";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    default: return "other";
    }
}

static void pm_set(int max_mhz, int min_mhz, bool light_sleep)
{
    esp_pm_config_t cfg = {
        .max_freq_mhz = max_mhz,
        .min_freq_mhz = min_mhz,
        .light_sleep_enable = light_sleep,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_configure(&cfg));
}

void runner_init(void)
{
    s_reset_reason = esp_reset_reason();
    bool keep = s_rtc.magic == PT_MAGIC &&
                (s_reset_reason == ESP_RST_SW || s_reset_reason == ESP_RST_DEEPSLEEP);
    if (!keep) {
        s_rtc = (pt_rtc_t){
            .magic = PT_MAGIC,
            .settings = DEFAULTS,
            .duration_s = 60,
            .seq_next = -1,
        };
    }
    s_finished_test = s_rtc.last_test < PT_TEST_COUNT ? (pt_test_t)s_rtc.last_test : PT_TEST_NONE;
    s_finished_s = s_rtc.last_test_s;
    s_resume_step = s_rtc.seq_next;
    s_rtc.last_test = PT_TEST_NONE;
    s_rtc.seq_next = -1;
    ESP_LOGI(TAG, "reset: %s, finished test: %s, resume step: %d", reset_reason_name(s_reset_reason),
             s_finished_test ? TEST_NAMES[s_finished_test] : "none", s_resume_step);
}

const pt_settings_t *runner_settings(void)
{
    return &s_rtc.settings;
}

void runner_apply(const pt_settings_t *s)
{
    bool all = !s_applied_valid;
    if (all || s->cpu_mhz != s_applied.cpu_mhz) {
        pm_set(s->cpu_mhz, s->cpu_mhz, false);
    }
    if (all || s->pclk_mhz != s_applied.pclk_mhz) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(board_lcd_set_pclk(s->pclk_mhz * 1000000u));
    }
    if (all || s->animate != s_applied.animate) {
        ui_set_animate(s->animate);
    }
    if (all || s->wifi != s_applied.wifi) {
        net_set_mode(s->wifi);
    }
    board_backlight_set(s->backlight_pct);
    s_applied = *s;
    s_applied.wifi = net_get_mode(); /* may have been refused (no SSID) */
    s_applied_valid = true;
    s_rtc.settings = s_applied;
    ESP_LOGI(TAG, "STATE backlight=%u%% cpu=%u pclk=%u animate=%d wifi=%d", s_applied.backlight_pct,
             s_applied.cpu_mhz, s_applied.pclk_mhz, s_applied.animate, s_applied.wifi);
    ui_sync_controls();
}

/* Never returns: ends in esp_restart() or deep sleep. */
static void run_screen_off(pt_test_t test, uint16_t secs, const char *title)
{
    char body[160];
    snprintf(body, sizeof(body),
             "Screen goes dark in 3 s and stays dark for %u s,\nthen the board reboots to this menu.\n"
             "Press RESET to abort.", secs);
    lvgl_port_lock(0);
    ui_show_overlay(title ? title : TEST_NAMES[test], body, false);
    ui_set_overlay_countdown(-1);
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(3000));

    s_rtc.last_test = test;
    s_rtc.last_test_s = secs;

    /* Freeze LVGL: stop its tick timer and keep its lock, so the LVGL task
     * blocks on the mutex and never touches the panel or touch again. */
    lvgl_port_lock(0);
    lvgl_port_stop();

    int cpu = s_rtc.settings.cpu_mhz;
    uint64_t sleep_s = secs;
    if (test != PT_TEST_WARM_STANDBY) {
        net_set_mode(NET_OFF);
    }
    net_keepalive_stop();
    board_backlight_set(0);

    ESP_LOGI(TAG, "TEST %s: start at %lld ms, %u s", TEST_NAMES[test], esp_timer_get_time() / 1000, secs);
    if (test == PT_TEST_BACKLIGHT_OFF) {
        goto dwell_and_restart;
    }

    board_panel_power(false);
    board_lcd_delete();
    if (test == PT_TEST_PANEL_OFF) {
        pm_set(cpu, cpu, false);
        goto dwell_and_restart;
    }

    board_touch_sleep();
    if (test == PT_TEST_TOUCH_SLEEP) {
        pm_set(cpu, cpu, false);
        goto dwell_and_restart;
    }
    if (test == PT_TEST_LIGHT_SLEEP) {
        pm_set(cpu, 40, true);
        goto dwell_and_restart;
    }
    if (test == PT_TEST_WARM_STANDBY) {
        pm_set(cpu, 40, true);
        net_set_mode(NET_ON_MAX_PS);
        if (!net_wait_ip(20000)) {
            ESP_LOGW(TAG, "warm standby: no IP after 20 s, measuring anyway");
        }
        net_keepalive_start(5000);
        ESP_LOGI(TAG, "warm standby: associated at %lld ms, dwell starts now", esp_timer_get_time() / 1000);
        goto dwell_and_restart;
    }

    /* Deep-sleep family */
    if (test == PT_TEST_DEEP_SLEEP_PARKED) {
        board_lcd_pins_park();
    } else if (test == PT_TEST_WAKE_LATENCY) {
        sleep_s = 3;
        s_rtc.last_test_s = 3;
        if (net_available()) {
            s_rtc.settings.wifi = NET_ON_MIN_PS; /* so the wake also times Wi-Fi */
        }
    }
    esp_sleep_enable_timer_wakeup(sleep_s * 1000000ull);
    esp_deep_sleep_start();

dwell_and_restart:
    vTaskDelay(pdMS_TO_TICKS((uint32_t)secs * 1000));
    ESP_LOGI(TAG, "TEST %s: done, restarting", TEST_NAMES[test]);
    esp_restart();
}

static bool step_needs_wifi(const seq_step_t *st)
{
    return st->settings.wifi != NET_OFF || st->test == PT_TEST_WARM_STANDBY;
}

static void run_sequence(int start)
{
    const uint16_t dwell = CONFIG_PT_SEQUENCE_STEP_S;
    char title[80];
    for (int i = start; i < SEQ_LEN; i++) {
        const seq_step_t *st = &SEQ[i];
        if (step_needs_wifi(st) && !net_available()) {
            ESP_LOGI(TAG, "STEP %d/%d skipped: no Wi-Fi configured", i + 1, SEQ_LEN);
            continue;
        }
        const char *name = st->name ? st->name : TEST_NAMES[st->test];
        snprintf(title, sizeof(title), "Step %d/%d: %s", i + 1, SEQ_LEN, name);
        ESP_LOGI(TAG, "STEP %d/%d %s: start at %lld ms", i + 1, SEQ_LEN, name, esp_timer_get_time() / 1000);

        if (st->test != PT_TEST_NONE) {
            s_rtc.seq_next = i + 1;
            lvgl_port_lock(0);
            runner_apply(&st->settings);
            lvgl_port_unlock();
            run_screen_off(st->test, dwell, title);
        }

        lvgl_port_lock(0);
        runner_apply(&st->settings);
        ui_show_overlay(title, "Read the meter once the value settles.\nTap anywhere to abort the sequence.", true);
        lvgl_port_unlock();
        for (int left = dwell; left > 0; left--) {
            lvgl_port_lock(0);
            ui_set_overlay_countdown(left);
            lvgl_port_unlock();
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (s_abort) {
                ESP_LOGI(TAG, "sequence aborted at step %d", i + 1);
                lvgl_port_lock(0);
                ui_hide_overlay();
                lvgl_port_unlock();
                return;
            }
        }
    }
    lvgl_port_lock(0);
    ui_hide_overlay();
    lvgl_port_unlock();
}

static void runner_task(void *arg)
{
    if (s_resume_step >= 0 && s_resume_step < SEQ_LEN) {
        s_busy = true;
        run_sequence(s_resume_step);
        s_busy = false;
    }
    cmd_t cmd;
    while (xQueueReceive(s_queue, &cmd, portMAX_DELAY)) {
        s_abort = false;
        s_busy = true;
        if (cmd.sequence) {
            run_sequence(0);
        } else {
            run_screen_off(cmd.test, s_rtc.duration_s, NULL);
        }
        s_busy = false;
    }
}

void runner_start(void)
{
    s_queue = xQueueCreate(2, sizeof(cmd_t));
    xTaskCreate(runner_task, "runner", 4096, NULL, 3, NULL);
}

void runner_start_test(pt_test_t test)
{
    if (s_busy || test == PT_TEST_NONE || test >= PT_TEST_COUNT) {
        return;
    }
    cmd_t cmd = {.sequence = false, .test = test};
    xQueueSend(s_queue, &cmd, 0);
}

void runner_start_sequence(void)
{
    if (s_busy) {
        return;
    }
    cmd_t cmd = {.sequence = true};
    xQueueSend(s_queue, &cmd, 0);
}

void runner_abort(void)
{
    s_abort = true;
}

bool runner_busy(void)
{
    return s_busy;
}

uint16_t runner_duration_s(void)
{
    return s_rtc.duration_s;
}

void runner_set_duration_s(uint16_t seconds)
{
    s_rtc.duration_s = seconds;
}

const char *runner_test_name(pt_test_t test)
{
    return test < PT_TEST_COUNT ? TEST_NAMES[test] : "";
}

void runner_mark_first_frame(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    int n = snprintf(s_boot_note, sizeof(s_boot_note), "Reset: %s\nBoot to first frame: %lld ms",
                     reset_reason_name(s_reset_reason), now_ms);
    if (s_finished_test != PT_TEST_NONE && n < (int)sizeof(s_boot_note)) {
        n += snprintf(s_boot_note + n, sizeof(s_boot_note) - n, "\nJust finished: %s (%u s)",
                      TEST_NAMES[s_finished_test], s_finished_s);
    }
    if (s_resume_step >= SEQ_LEN && n < (int)sizeof(s_boot_note)) {
        snprintf(s_boot_note + n, sizeof(s_boot_note) - n, "\nSequence complete.");
    } else if (s_resume_step >= 0 && n < (int)sizeof(s_boot_note)) {
        snprintf(s_boot_note + n, sizeof(s_boot_note) - n, "\nResuming sequence at step %d/%d",
                 s_resume_step + 1, SEQ_LEN);
    }
    ESP_LOGI(TAG, "first frame at %lld ms", now_ms);
}

const char *runner_boot_note(void)
{
    return s_boot_note;
}
