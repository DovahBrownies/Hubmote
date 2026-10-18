/*
 * Settings > Screen and Appearance, and what follows from them: brightness,
 * the turn (Rotate 180°), dimming and turning the screen off when idle
 * (longer on USB power), turning the remote off after idle hours, the
 * theme (applied with a restart), and the battery readout. Part of the
 * controller (ctrl_internal.h).
 */
#include "atv_remote.h"
#include "battery.h"
#include "board.h"
#include "ctrl_internal.h"
#include "display.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "hub.h"
#include "kv.h"
#include "sdkconfig.h"
#include "ui.h"

static const char *TAG = "ctrl";

#define BATTERY_EVERY_MS 200 /* battery.c expects five readings a second */
/* Settings > Screen; the menuconfig values are the defaults. */
#define NVS_BRIGHTNESS   "bright"
#define NVS_DIM_S        "dim_s"
#define NVS_OFF_S        "off_s"
#define NVS_DIM_C        "dim_c" /* while charging; 0 = never */
#define NVS_OFF_C        "off_c"
#define NVS_SLEEP_H      "sleep_h"
#define NVS_THEME        "theme" /* Settings > Appearance, applied at start-up */
#define REOPEN_MAGIC     0x7E3E0A11u
#define SAVE_AFTER_US    (2 * 1000000LL) /* brightness: save once the slider rests */

static bool s_asleep;
static int64_t s_last_battery_us;
static int s_brightness = CONFIG_HM_BRIGHTNESS;
static int64_t s_brightness_changed_us; /* not saved yet, 0 = saved */
static int s_dim_s = CONFIG_HM_DIM_AFTER_S;
static int s_off_s = CONFIG_HM_SCREEN_OFF_AFTER_S;
static int s_sleep_h = CONFIG_HM_SLEEP_AFTER_H; /* 0 = never */
static int s_dim_c_s = CONFIG_HM_DIM_AFTER_CHARGING_S; /* on USB power; 0 = never */
static int s_off_c_s = CONFIG_HM_SCREEN_OFF_AFTER_CHARGING_S;
static bool s_on_usb; /* charging, or no cell at all (then USB is the only power) */
static char s_theme[16];
/* Set before restarting for a new theme, so Settings > Appearance opens
 * again once the remote is back (survives esp_restart, not power-off). */
static RTC_NOINIT_ATTR uint32_t s_reopen_appearance;
static bool s_show_appearance;

/* --- Settings in NVS --------------------------------------------------------------------- */

static void load_pref(const char *key, int *value, int min, int max)
{
    int32_t v;
    if (kv_get_i32(CTRL_NVS_NS, key, &v) == ESP_OK && v >= min && v <= max) {
        *value = (int)v;
    }
}

int controller_brightness(void)
{
    return s_brightness;
}

void ctrl_screen_load(void)
{
    /* The same ranges as main/Kconfig.projbuild: a saved value outside them
     * keeps the default. Settings' choices lie inside. */
    load_pref(NVS_BRIGHTNESS, &s_brightness, 1, 100);
    load_pref(NVS_DIM_S, &s_dim_s, 5, 3600);
    load_pref(NVS_OFF_S, &s_off_s, 10, 3600);
    load_pref(NVS_SLEEP_H, &s_sleep_h, 0, 168);
    load_pref(NVS_DIM_C, &s_dim_c_s, 0, 7200);
    load_pref(NVS_OFF_C, &s_off_c_s, 0, 7200);
    kv_get_str(CTRL_NVS_NS, NVS_THEME, s_theme, sizeof(s_theme));
    ESP_LOGI(TAG,
             "screen: brightness %d%%, dim after %d s, off after %d s (charging: %d s, %d s; 0 = never), "
             "turn off after %d h, theme %s",
             s_brightness, s_dim_s, s_off_s, s_dim_c_s, s_off_c_s, s_sleep_h, s_theme[0] ? s_theme : "default");
    s_show_appearance = s_reopen_appearance == REOPEN_MAGIC;
    s_reopen_appearance = 0;
    ui_set_theme(s_theme);
}

void ctrl_screen_show(void)
{
    ui_set_brightness(s_brightness);
    ui_set_screen_turned(display_is_turned());
    ui_timeouts_t timeouts = {.dim_s = s_dim_s,
                              .off_s = s_off_s,
                              .dim_charging_s = s_dim_c_s,
                              .off_charging_s = s_off_c_s,
                              .sleep_h = s_sleep_h};
    ui_set_screen_timeouts(&timeouts);
    ui_set_board_caps(BOARD_BACKLIGHT_DIMMABLE, BOARD_BATTERY_READING);
}

static void save_pref(const char *key, int value)
{
    kv_set_i32(CTRL_NVS_NS, key, value);
    ESP_LOGI(TAG, "setting %s = %d", key, value);
}

void ctrl_screen_connected(void)
{
    if (s_show_appearance) {
        s_show_appearance = false;
        ui_show_appearance();
    }
}

/* --- Idle, dimming, the battery --------------------------------------------------------- */

void ctrl_screen_tick(int64_t now)
{
    static const char *const names[] = {"on", "dimmed", "off"};
    static int s_screen = 0; /* 0 on, 1 dimmed, 2 off */
    uint32_t idle = ui_idle_ms();
    if (s_sleep_h > 0 && idle >= (uint32_t)s_sleep_h * 3600u * 1000u && !hub_awake_held()) {
        ESP_LOGI(TAG, "unused for %d h: turning off", s_sleep_h);
        atv_disconnect();
        board_deep_sleep();
    }
    /* On USB power the screen stays bright longer (0 = never). */
    uint32_t dim_s = (uint32_t)(s_on_usb ? s_dim_c_s : s_dim_s);
    uint32_t off_s = (uint32_t)(s_on_usb ? s_off_c_s : s_off_s);
    int screen = off_s && idle >= off_s * 1000                              ? 2
                 : BOARD_BACKLIGHT_DIMMABLE && dim_s && idle >= dim_s * 1000 ? 1
                                                                             : 0;
    static int s_refresh_ticks;
    if (screen != s_screen) {
        ESP_LOGI(TAG, "screen %s -> %s (idle %lu ms)", names[s_screen], names[screen], (unsigned long)idle);
        if (screen == 2 || s_screen == 2) {
            hub_modules_event(screen == 2 ? HUB_EV_SCREEN_OFF : HUB_EV_SCREEN_ON);
        }
        s_screen = screen;
        s_refresh_ticks = 3; /* re-send on the next tick, after the change below */
    }
    /* The expander now and then drops a write; re-send the wanted state
     * about once a second so a missed wake-up fixes itself. */
    if (++s_refresh_ticks >= 5) {
        s_refresh_ticks = 0;
        board_backlight_refresh();
    }

    if (screen == 2) {
        if (!s_asleep) {
            s_asleep = true;
            ui_set_asleep(true);
            board_backlight_set(0);
        }
    } else if (screen == 1) {
        board_backlight_set(CONFIG_HM_BRIGHTNESS_DIM < s_brightness ? CONFIG_HM_BRIGHTNESS_DIM : s_brightness);
    } else {
        s_asleep = false;
        board_backlight_set((uint8_t)s_brightness);
    }

    if (s_brightness_changed_us && now - s_brightness_changed_us >= SAVE_AFTER_US) {
        s_brightness_changed_us = 0;
        save_pref(NVS_BRIGHTNESS, s_brightness);
    }
    if (now - s_last_battery_us >= BATTERY_EVERY_MS * 1000LL || s_last_battery_us == 0) {
        s_last_battery_us = now;
        static battery_state_t s_shown = {.percent = -2};
        battery_state_t b = battery_update(now, board_battery_mv(), board_backlight_get() > 0, board_backlight_changed_us());
        /* A board that can't read its battery may well run on it: the
         * battery times apply. */
        s_on_usb = BOARD_BATTERY_READING && (b.charging || b.no_cell);
#if CONFIG_HM_BATTERY_PREVIEW
        static int64_t s_preview_us;
        if (!b.charging && b.percent >= 0) {
            s_preview_us = s_preview_us ? s_preview_us : now;
            int pct = 35 - (int)((now - s_preview_us) / 2000000);
            b.percent = pct < 10 ? 10 : pct;
        }
#endif
        if (b.percent != s_shown.percent || b.charging != s_shown.charging) {
            s_shown = b;
            ui_set_battery(b.percent, b.charging);
        }
    }
}

/* --- UI actions --------------------------------------------------------------------------- */

bool ctrl_screen_ui(const ctrl_ui_t *a)
{
    switch (a->type) {
    case UI_ACT_POWER_OFF:
        ESP_LOGI(TAG, "turning off");
        atv_disconnect();
        board_deep_sleep();
        return true;
    case UI_ACT_SCREEN_TURN:
        display_set_turned(a->index != 0);
        return true;
    case UI_ACT_BRIGHTNESS:
        s_brightness = a->index < 1 ? 1 : (a->index > 100 ? 100 : a->index);
        board_backlight_set((uint8_t)s_brightness);
        s_brightness_changed_us = esp_timer_get_time();
        return true;
    case UI_ACT_DIM_AFTER:
        s_dim_s = a->index;
        save_pref(NVS_DIM_S, s_dim_s);
        return true;
    case UI_ACT_SCREEN_OFF_AFTER:
        s_off_s = a->index;
        save_pref(NVS_OFF_S, s_off_s);
        return true;
    case UI_ACT_SLEEP_AFTER:
        s_sleep_h = a->index;
        save_pref(NVS_SLEEP_H, s_sleep_h);
        return true;
    case UI_ACT_DIM_AFTER_CHARGING:
        s_dim_c_s = a->index;
        save_pref(NVS_DIM_C, s_dim_c_s);
        return true;
    case UI_ACT_SCREEN_OFF_AFTER_CHARGING:
        s_off_c_s = a->index;
        save_pref(NVS_OFF_C, s_off_c_s);
        return true;
    case UI_ACT_THEME: {
        /* Screens take their colors when they are built, so a new theme
         * applies with a restart (about 10 s back to the TV). */
        kv_set_str(CTRL_NVS_NS, NVS_THEME, a->text);
        ESP_LOGI(TAG, "theme %s: restarting", a->text);
        s_reopen_appearance = REOPEN_MAGIC;
        atv_disconnect();
        esp_restart();
        return true;
    }
    default:
        return false;
    }
}
