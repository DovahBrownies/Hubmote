/*
 * Sleep timer: turns the TV off after 15 to 120 minutes. The Streamer goes
 * to sleep and takes the TV with it over HDMI-CEC, or the TV's own IR off
 * key does it (Settings > Modules > Sleep timer). A minute before, a
 * message says so, on the TV too (companion app 0.7+); while it runs, the
 * top bar counts down, and the remote doesn't turn itself off.
 *
 * Threads: the buttons and the timer's work run on the LVGL task (the
 * esp_timer callback only hands over: its stack is small), the events on
 * the controller task; s_mux guards the state.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "hub.h"
#include "sdkconfig.h"
#include "ui_kit.h"

#define MODULE_ID "sleep_timer"
#define WARN_US   (60 * 1000000LL)
#define MIN_US    (60 * 1000000LL)

static const char *TAG = "sleep_timer";

static const int k_minutes[] = {
#if CONFIG_HM_DEVELOPER
    1, /* developer builds: for quick tests */
#endif
    15, 30, 45, 60, 90, 120,
};
#define PRESETS (sizeof(k_minutes) / sizeof(k_minutes[0]))

static const char *const k_off_with[] = {"Google TV", "TV's IR"};
static const hub_setting_t k_settings[] = {
    {.key = "off_ir", .label = "Turn off with", .type = HUB_SETTING_CHOICE, .choices = k_off_with, .count = 2},
    {.key = "warn", .label = "Warn a minute before", .type = HUB_SETTING_SWITCH, .def = 1},
    HUB_SETTING_SHOW_ON_TV,
};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_end_us; /* esp_timer time it goes off; 0 = not running */
static bool s_warned;
static bool s_held;      /* hub_hold_awake() while running */
static esp_timer_handle_t s_timer;

/* Screen and card (LVGL task) */
static lv_obj_t *s_card_value;
static lv_obj_t *s_card_detail;
static lv_obj_t *s_view_value;
static lv_obj_t *s_view_unit;
static lv_obj_t *s_view_off;
static lv_obj_t *s_view_detail;
static lv_obj_t *s_running_row;

static const hub_module_t *self(void)
{
    return hub_module_find(MODULE_ID);
}

/* Minutes left, rounded up; 0 when not running. */
static int minutes_left(void)
{
    taskENTER_CRITICAL(&s_mux);
    int64_t end = s_end_us;
    taskEXIT_CRITICAL(&s_mux);
    if (!end) {
        return 0;
    }
    int64_t left = end - esp_timer_get_time();
    return left <= 0 ? 0 : (int)((left + MIN_US - 1) / MIN_US);
}

/* "TV off at 22:45" when the clock is set; "" otherwise. */
static void off_at(char *out, size_t len, int minutes)
{
    struct tm now;
    out[0] = '\0';
    if (minutes > 0 && hub_time_local(&now)) {
        int at = (now.tm_hour * 60 + now.tm_min + minutes) % (24 * 60);
        snprintf(out, len, "TV off at %02d:%02d", at / 60, at % 60);
    }
}

/* --- UI (LVGL task) ------------------------------------------------------------ */

static void show(void *arg)
{
    int left = minutes_left();
    char detail[24];
    off_at(detail, sizeof(detail), left);
    if (s_card_value) {
        if (left) {
            lv_label_set_text_fmt(s_card_value, "%d min", left);
        } else {
            lv_label_set_text(s_card_value, "Off");
        }
        lv_label_set_text(s_card_detail, detail);
    }
    if (s_view_value) {
        lv_label_set_text_fmt(s_view_value, "%d", left);
        lv_obj_set_hidden(s_view_value, !left);
        lv_obj_set_hidden(s_view_unit, !left);
        lv_obj_set_hidden(s_view_off, left);
        lv_label_set_text(s_view_detail, detail);
        lv_obj_set_hidden(s_running_row, !left);
    }
}

static void refresh(void)
{
    hub_ui_async(show, NULL);
    hub_glance_changed();
}

/* --- The timer ------------------------------------------------------------------- */

/* Wakes at the warning, then at the end. */
static void arm(void)
{
    taskENTER_CRITICAL(&s_mux);
    int64_t end = s_end_us;
    bool warn_due = !s_warned;
    taskEXIT_CRITICAL(&s_mux);
    esp_timer_stop(s_timer);
    if (!end) {
        return;
    }
    int64_t at = warn_due && end - WARN_US > esp_timer_get_time() ? end - WARN_US : end;
    int64_t delay = at - esp_timer_get_time();
    esp_timer_start_once(s_timer, delay > 0 ? (uint64_t)delay : 1);
}

static void hold(bool on)
{
    if (on && !s_held) {
        hub_hold_awake();
    } else if (!on && s_held) {
        hub_release_awake();
    }
    s_held = on;
}

/* The warning or the end (LVGL task, from on_timer). */
static void tick(void *arg)
{
    taskENTER_CRITICAL(&s_mux);
    int64_t end = s_end_us;
    taskEXIT_CRITICAL(&s_mux);
    if (!end) {
        return;
    }
    if (esp_timer_get_time() < end) {
        taskENTER_CRITICAL(&s_mux);
        s_warned = true;
        taskEXIT_CRITICAL(&s_mux);
        if (hub_setting_i32(self(), "warn")) {
            hub_notify(self(), ICON_W_MOON, "The TV turns off in a minute");
        }
        arm();
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    s_end_us = 0;
    taskEXIT_CRITICAL(&s_mux);
    bool ir = hub_setting_i32(self(), "off_ir") != 0;
    esp_err_t err = hub_tv_off(ir);
    ESP_LOGI(TAG, "time's up: TV off via %s (%s)", ir ? "IR" : "the Streamer", esp_err_to_name(err));
    hold(false);
    refresh();
}

static void on_timer(void *arg)
{
    hub_ui_async(tick, NULL);
}

static void start(int minutes)
{
    int64_t end = esp_timer_get_time() + (int64_t)minutes * MIN_US;
    taskENTER_CRITICAL(&s_mux);
    s_end_us = end;
    s_warned = false;
    taskEXIT_CRITICAL(&s_mux);
    hold(true);
    arm();
    ESP_LOGI(TAG, "%d min", minutes);
    refresh();
}

static void extend(int minutes)
{
    taskENTER_CRITICAL(&s_mux);
    int64_t end = s_end_us;
    if (end) {
        s_end_us = end + (int64_t)minutes * MIN_US;
        s_warned = false;
    }
    taskEXIT_CRITICAL(&s_mux);
    if (!end) {
        start(minutes);
        return;
    }
    arm();
    ESP_LOGI(TAG, "%d min more", minutes);
    refresh();
}

static void cancel(void)
{
    taskENTER_CRITICAL(&s_mux);
    s_end_us = 0;
    taskEXIT_CRITICAL(&s_mux);
    esp_timer_stop(s_timer);
    hold(false);
    ESP_LOGI(TAG, "cancelled");
    refresh();
}

/* --- Screen and card (LVGL task) ------------------------------------------------- */

static void on_preset(lv_event_t *e)
{
    start(k_minutes[(intptr_t)lv_event_get_user_data(e)]);
}

static void on_more(lv_event_t *e)
{
    extend(15);
}

static void on_cancel(lv_event_t *e)
{
    cancel();
}

static lv_obj_t *create_card(lv_obj_t *parent, hub_card_size_t size)
{
    lv_obj_t *card = ui_panel(parent, HUB_CARD_W(size), HUB_CARD_H);
    lv_obj_set_style_pad_all(card, UI_S(22), 0);
    ui_flex(card, LV_FLEX_FLOW_COLUMN, UI_S(8));
    lv_obj_t *head = ui_box(card);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(10));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, &font_icons_24, COL_MOON, ICON_W_MOON);
    ui_label(head, &font_sora_16sb, COL_TEXT, "Sleep timer");
    lv_obj_t *spacer = ui_box(card);
    lv_obj_set_flex_grow(spacer, 1);
    s_card_value = ui_label(card, &font_sora_26m, COL_TEXT, "");
    s_card_detail = ui_label(card, &font_sora_13m, COL_TEXT2, "");
    show(NULL);
    return card;
}

static lv_obj_t *create_view(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, HUB_VIEW_W, HUB_VIEW_H);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(18));
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* The time left: "42 min", or "Off" */
    lv_obj_t *now = ui_box(p);
    lv_obj_set_size(now, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(now, LV_FLEX_FLOW_ROW, UI_S(10));
    lv_obj_set_flex_align(now, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_view_value = ui_label(now, &font_sora_88n, COL_TEXT, "");
    s_view_unit = ui_label(now, &font_sora_24m, COL_TEXT2, "min");
    lv_obj_set_style_pad_bottom(s_view_unit, UI_S(14), 0);
    s_view_off = ui_label(now, &font_sora_26m, COL_TEXT2, "Off");
    s_view_detail = ui_label(p, &font_sora_15m, COL_TEXT2, "");

    /* 15 to 120 minutes */
    lv_obj_t *presets = ui_box(p);
    lv_obj_set_size(presets, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(presets, LV_FLEX_FLOW_ROW, UI_S(12));
    for (size_t i = 0; i < PRESETS; i++) {
        char text[12];
        snprintf(text, sizeof(text), "%d min", k_minutes[i]);
        lv_obj_t *b = ui_btn_text(presets, UI_S(120), UI_S(64), 16, COL_SURFACE2, &font_sora_16sb, COL_TEXT, text);
        lv_obj_add_event_cb(b, on_preset, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    /* While it runs */
    s_running_row = ui_box(p);
    lv_obj_set_size(s_running_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(s_running_row, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_t *more = ui_btn_icon(s_running_row, UI_S(200), UI_S(56), 16, COL_SURFACE2, &font_icons_20, ICON_PLUS,
                                 COL_TEXT, "15 min", false);
    lv_obj_add_event_cb(more, on_more, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop = ui_btn_icon(s_running_row, UI_S(200), UI_S(56), 16, COL_SURFACE2, &font_icons_20, ICON_CLOSE,
                                 COL_TEXT, "Cancel", false);
    lv_obj_add_event_cb(stop, on_cancel, LV_EVENT_CLICKED, NULL);
    show(NULL);
    return p;
}

static bool glance(hub_glance_t *out)
{
    int left = minutes_left();
    if (!left) {
        return false;
    }
    out->icon = ICON_W_MOON;
    out->color = COL_MOON;
    snprintf(out->text, sizeof(out->text), "%d min", left);
    return true;
}

/* --- Module (controller task) ----------------------------------------------------- */

static void init(void)
{
    const esp_timer_create_args_t args = {.callback = on_timer, .name = "sleep_timer"};
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        ESP_LOGE(TAG, "no timer");
    }
}

/* Switched off while running: the timer stops (LVGL task, as its buttons). */
static void stop_ui(void *arg)
{
    if (minutes_left()) {
        cancel();
    }
}

static void event(hub_event_t ev)
{
    switch (ev) {
    case HUB_EV_MINUTE:
    case HUB_EV_SCREEN_ON:
        if (minutes_left()) {
            refresh();
        }
        break;
    case HUB_EV_NETWORK_UP:
    case HUB_EV_NETWORK_DOWN:
    case HUB_EV_SCREEN_OFF:
    case HUB_EV_NOW_PLAYING:
    case HUB_EV_TV:
        break;
    case HUB_EV_OFF:
        hub_ui_async(stop_ui, NULL);
        break;
    }
}

static const hub_module_t s_module = {
    .id = MODULE_ID,
    .name = "Sleep timer",
    .icon = ICON_W_MOON,
    .init = init,
    .event = event,
    .create_view = create_view,
    .create_card = create_card,
    .glance = glance,
    .card_sizes = HUB_CARD_SMALL | HUB_CARD_MEDIUM | HUB_CARD_LARGE,
    .settings = k_settings,
    .settings_count = sizeof(k_settings) / sizeof(k_settings[0]),
};
HUB_MODULE(s_module);
