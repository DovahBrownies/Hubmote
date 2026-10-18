/*
 * Screen time: how long the TV was on today, and in which apps, against a
 * daily limit (Settings > Modules > Screen time). Before the limit a
 * message warns; at it, another says time is up and, a minute later, the
 * TV turns off, again each time it comes back on that day. Messages show
 * on the TV too (companion app 0.7+). A new day starts at local midnight.
 *
 * It counts a minute at each of the controller's minute ticks while the
 * Streamer is on (or, with Count while Playing, while the companion app
 * says something plays), so it only counts while the remote is on: with a
 * limit set it keeps the remote from turning itself off while the TV is on.
 *
 * Threads: counting, the TV's events and settings run on the controller
 * task; the screen, the card and + 15 min on the LVGL task, and the off
 * timer hands over to it too (an esp_timer callback's stack is small).
 * check() runs on both. s_mux guards s and the two message flags.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "hub.h"
#include "sdkconfig.h"
#include "ui_kit.h"

#define MODULE_ID  "screen_time"
#define APPS_MAX   8  /* apps counted a day, "Other" included */
#define ROWS       6  /* apps on its screen */
#define SAVE_EVERY 5  /* counted minutes between saves to NVS */
#define EXTRA_MIN  15 /* + 15 min */
#define GRACE_US   (60 * 1000000LL)
#define NAMES_MAX  48 /* the companion's app list */
#define LAUNCHER   "com.google.android.apps.tv.launcherx"

static const char *TAG = "screen_time";

enum { AT_LIMIT_OFF, AT_LIMIT_WARN };
enum { COUNT_TV_ON, COUNT_PLAYING };

static const char *const k_at_limit[] = {"Turn TV off", "Only warn"};
static const char *const k_off_with[] = {"Google TV", "TV's IR"};
static const char *const k_count[] = {"TV is on", "Playing"};
static const int32_t k_limits[] = {
    0,
#if CONFIG_HM_DEVELOPER
    1, /* developer builds: for quick tests */
#endif
    15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180, 210, 240, 270, 300, 360, 420, 480, 540, 600,
};
static const hub_setting_t k_settings[] = {
    {.key = "limit", .label = "Daily limit", .type = HUB_SETTING_NUMBER, .def = 0, .values = k_limits,
     .count = sizeof(k_limits) / sizeof(k_limits[0]), .unit = "min", .zero = "Off"},
    {.key = "at_limit", .label = "At the limit", .type = HUB_SETTING_CHOICE, .choices = k_at_limit, .count = 2},
    {.key = "off_ir", .label = "Turn off with", .type = HUB_SETTING_CHOICE, .choices = k_off_with, .count = 2},
    {.key = "warn", .label = "Warn before the limit", .type = HUB_SETTING_NUMBER, .def = 10, .min = 0, .max = 30,
     .step = 5, .unit = "min", .zero = "Off"},
    {.key = "count", .label = "Count while", .type = HUB_SETTING_CHOICE, .choices = k_count, .count = 2},
    HUB_SETTING_SHOW_ON_TV,
};

typedef struct {
    char app[HUB_APP_ID_MAX]; /* "" = other apps */
    int minutes;
} app_use_t;

typedef struct {
    int32_t day; /* yyyymmdd the counts are for; 0 = not known yet */
    int used;    /* minutes today */
    int extra;   /* minutes added today with + 15 min */
    int apps;
    app_use_t app[APPS_MAX];
} today_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static today_t s;
static bool s_warned;   /* the warning before the limit was given */
static bool s_told;     /* the limit message was given since the TV came on */
static bool s_off_sent; /* the TV was turned off since then */

/* Controller task */
static bool s_tv_on;
static bool s_held;
static int s_unsaved;
static esp_timer_handle_t s_off_timer;

/* Screen and card (LVGL task) */
static lv_obj_t *s_card_value;
static lv_obj_t *s_card_detail;
static lv_obj_t *s_card_bar;
static lv_obj_t *s_view_h;
static lv_obj_t *s_view_h_unit;
static lv_obj_t *s_view_m;
static lv_obj_t *s_view_m_unit;
static lv_obj_t *s_view_detail;
static lv_obj_t *s_view_bar;
static lv_obj_t *s_view_extra;
static lv_obj_t *s_rows[ROWS];
static lv_obj_t *s_row_name[ROWS];
static lv_obj_t *s_row_value[ROWS];
static lv_obj_t *s_row_bar[ROWS];
static lv_obj_t *s_apps_status;

static const hub_module_t *self(void)
{
    return hub_module_find(MODULE_ID);
}

static int setting(const char *key)
{
    return hub_setting_i32(self(), key);
}

/* "1 h 25 min", "45 min", "2 h" */
static void fmt_minutes(char *out, size_t len, int minutes)
{
    if (minutes < 60) {
        snprintf(out, len, "%d min", minutes);
    } else if (minutes % 60 == 0) {
        snprintf(out, len, "%d h", minutes / 60);
    } else {
        snprintf(out, len, "%d h %d min", minutes / 60, minutes % 60);
    }
}

/* Minutes left today: INT_MAX without a limit, 0 or less once it's reached. */
static int minutes_left(void)
{
    int limit = setting("limit");
    taskENTER_CRITICAL(&s_mux);
    int left = limit > 0 ? limit + s.extra - s.used : INT_MAX;
    taskEXIT_CRITICAL(&s_mux);
    return left;
}

/* --- Storage (controller task) ------------------------------------------------ */

static void save(void)
{
    static today_t t;
    static char apps[APPS_MAX * (HUB_APP_ID_MAX + 8)];
    taskENTER_CRITICAL(&s_mux);
    t = s;
    taskEXIT_CRITICAL(&s_mux);
    size_t at = 0;
    apps[0] = '\0';
    for (int i = 0; i < t.apps && at < sizeof(apps); i++) {
        at += snprintf(apps + at, sizeof(apps) - at, "%s=%d;", t.app[i].app, t.app[i].minutes);
    }
    hub_kv_set_i32(MODULE_ID, "day", t.day);
    hub_kv_set_i32(MODULE_ID, "used", t.used);
    hub_kv_set_i32(MODULE_ID, "extra", t.extra);
    hub_kv_set_str(MODULE_ID, "apps", apps);
    s_unsaved = 0;
}

static void load(void)
{
    static today_t t;
    static char apps[APPS_MAX * (HUB_APP_ID_MAX + 8)];
    int32_t v;
    memset(&t, 0, sizeof(t));
    if (hub_kv_get_i32(MODULE_ID, "day", &v) == ESP_OK) {
        t.day = v;
    }
    if (hub_kv_get_i32(MODULE_ID, "used", &v) == ESP_OK) {
        t.used = v;
    }
    if (hub_kv_get_i32(MODULE_ID, "extra", &v) == ESP_OK) {
        t.extra = v;
    }
    /* "package=minutes;" for each app; "=minutes;" for the other apps */
    if (hub_kv_get_str(MODULE_ID, "apps", apps, sizeof(apps)) == ESP_OK) {
        char *p = apps;
        for (char *end; t.apps < APPS_MAX && (end = strchr(p, ';')); p = end + 1) {
            *end = '\0';
            char *eq = strrchr(p, '=');
            if (eq) {
                *eq = '\0';
                strlcpy(t.app[t.apps].app, p, HUB_APP_ID_MAX);
                t.app[t.apps].minutes = atoi(eq + 1);
                t.apps++;
            }
        }
    }
    taskENTER_CRITICAL(&s_mux);
    s = t;
    taskEXIT_CRITICAL(&s_mux);
}

/* --- Counting and the limit ------------------------------------------------------ */

static void refresh(void);

/* yyyymmdd, local; 0 while the clock isn't set. */
static int32_t today(void)
{
    struct tm now;
    if (!hub_time_local(&now)) {
        return 0;
    }
    return (now.tm_year + 1900) * 10000 + (now.tm_mon + 1) * 100 + now.tm_mday;
}

/* Starts a new day at midnight. A date that goes back is the time zone
 * being corrected (as when Weather first reports its place's), not a new
 * day: today's count stays. Controller task. */
static void roll_day(void)
{
    int32_t day = today();
    if (!day) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    int32_t was = s.day;
    bool new_day = was && day > was;
    if (new_day) {
        memset(&s, 0, sizeof(s));
        s_warned = false;
        s_told = false;
    }
    s.day = day;
    taskEXIT_CRITICAL(&s_mux);
    if (day != was) {
        ESP_LOGI(TAG, "%s", new_day ? "a new day" : "the date is set");
        save();
        refresh();
    }
}

/* The app being watched now, or false if nothing counts. */
static bool watching(char *app, size_t len)
{
    hub_tv_state_t tv;
    hub_tv_state(&tv);
    if (!tv.on) {
        return false;
    }
    if (setting("count") == COUNT_PLAYING) {
        hub_now_playing_t now;
        if (!hub_now_playing(&now) || !now.playing) {
            return false;
        }
        strlcpy(app, now.app, len);
    } else {
        strlcpy(app, tv.app, len);
    }
    return true;
}

/* Adds a minute to app, or to "other apps" once APPS_MAX - 1 have a row.
 * Under s_mux. */
static void add_minute(const char *app)
{
    int named = 0;
    for (int i = 0; i < s.apps; i++) {
        if (strcmp(s.app[i].app, app) == 0) {
            s.app[i].minutes++;
            return;
        }
        named += s.app[i].app[0] != '\0';
    }
    if (app[0] && named >= APPS_MAX - 1) {
        add_minute("");
        return;
    }
    if (s.apps < APPS_MAX) {
        strlcpy(s.app[s.apps].app, app, HUB_APP_ID_MAX);
        s.app[s.apps].minutes = 1;
        s.apps++;
    }
}

static void count_minute(void)
{
    char app[HUB_APP_ID_MAX];
    if (!watching(app, sizeof(app))) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    s.used++;
    add_minute(app);
    taskEXIT_CRITICAL(&s_mux);
    if (++s_unsaved >= SAVE_EVERY) {
        save();
    }
    refresh();
}

/* Messages and the off timer, from the time left. Any task. */
static void check(void)
{
    hub_tv_state_t tv;
    hub_tv_state(&tv);
    int left = minutes_left();
    int warn = setting("warn");
    bool turn_off = setting("at_limit") == AT_LIMIT_OFF;
    taskENTER_CRITICAL(&s_mux);
    if (left > warn) {
        s_warned = false; /* time was added, or the limit raised */
    }
    if (left > 0) {
        s_told = false;
        s_off_sent = false;
    }
    bool do_warn = tv.on && warn > 0 && left > 0 && left <= warn && !s_warned;
    bool do_tell = tv.on && left <= 0 && !s_told;
    bool still_on = tv.on && left <= 0 && turn_off && s_off_sent;
    s_warned |= do_warn;
    s_told |= do_tell;
    taskEXIT_CRITICAL(&s_mux);

    if (still_on) {
        /* Turned off a minute ago or more, yet on: the Streamer's sleep
         * again, which does nothing if it is asleep already. */
        hub_tv_key(HUB_KEY_SLEEP);
        ESP_LOGI(TAG, "still on: Google TV to sleep again");
    }

    if (left > 0 || !turn_off) {
        esp_timer_stop(s_off_timer);
    }
    if (do_warn) {
        char text[64];
        snprintf(text, sizeof(text), "%d minute%s of screen time left today", left, left == 1 ? "" : "s");
        hub_notify(self(), ICON_HOURGLASS, text);
        ESP_LOGI(TAG, "%d min left", left);
    }
    if (do_tell) {
        hub_notify(self(), ICON_HOURGLASS,
                   turn_off ? "Screen time is up for today. The TV turns off in a minute."
                            : "Screen time is up for today.");
        ESP_LOGI(TAG, "limit reached%s", turn_off ? ": the TV turns off in a minute" : "");
        if (turn_off) {
            esp_timer_stop(s_off_timer);
            esp_timer_start_once(s_off_timer, GRACE_US);
        }
    }
}

/* A minute after the limit message (LVGL task, from on_off_timer). With
 * the TV's IR, the Streamer goes to sleep too: it is what the count
 * follows, and waking it starts the minute again. */
static void off_due(void *arg)
{
    hub_tv_state_t tv;
    hub_tv_state(&tv);
    if (!tv.on || minutes_left() > 0 || setting("at_limit") != AT_LIMIT_OFF) {
        return;
    }
    bool ir = setting("off_ir") != 0;
    esp_err_t err = hub_tv_off(ir);
    if (ir) {
        hub_tv_key(HUB_KEY_SLEEP);
    }
    taskENTER_CRITICAL(&s_mux);
    s_off_sent = true;
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "time's up: TV off via %s (%s)", ir ? "IR and the Streamer" : "the Streamer",
             esp_err_to_name(err));
}

static void on_off_timer(void *arg)
{
    hub_ui_async(off_due, NULL);
}

/* Keeps the remote on while the TV is on and a limit is set. Controller task. */
static void update_hold(void)
{
    bool hold = s_tv_on && setting("limit") > 0;
    if (hold && !s_held) {
        hub_hold_awake();
    } else if (!hold && s_held) {
        hub_release_awake();
    }
    s_held = hold;
}

static void tv_changed(void)
{
    hub_tv_state_t tv;
    hub_tv_state(&tv);
    if (tv.on == s_tv_on) {
        return; /* another app */
    }
    s_tv_on = tv.on;
    update_hold();
    if (tv.on) {
        roll_day();
        check();
        return;
    }
    /* Off: tell again when it comes back on. */
    taskENTER_CRITICAL(&s_mux);
    s_warned = false;
    s_told = false;
    s_off_sent = false;
    taskEXIT_CRITICAL(&s_mux);
    esp_timer_stop(s_off_timer);
    if (s_unsaved) {
        save();
    }
}

/* --- Screen and card (LVGL task) ------------------------------------------------- */

static lv_obj_t *bar(lv_obj_t *parent, int32_t h)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, LV_PCT(100), h);
    lv_bar_set_range(b, 0, 1000);
    lv_obj_set_style_bg_color(b, COL_SURFACE3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, h / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
    lv_obj_set_clickable(b, false);
    return b;
}

static void set_bar(lv_obj_t *b, int value, int of, lv_color_t color)
{
    int permille = of > 0 ? (value >= of ? 1000 : value * 1000 / of) : 0;
    lv_bar_set_value(b, permille, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b, color, LV_PART_INDICATOR);
}

/* An app's name from the companion's list, "Home screen" for Google TV's
 * own, "Other apps" for the rest; the package if the list doesn't have it. */
static void app_name(const char *app, const hub_tv_app_t *names, size_t count, char *out, size_t len)
{
    if (!app[0]) {
        strlcpy(out, "Other apps", len);
        return;
    }
    if (strcmp(app, LAUNCHER) == 0) {
        strlcpy(out, "Home screen", len);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(names[i].app, app) == 0) {
            strlcpy(out, names[i].name, len);
            return;
        }
    }
    strlcpy(out, app, len);
}

static void show_apps(const today_t *t)
{
    static hub_tv_app_t *names; /* PSRAM */
    if (!names) {
        names = heap_caps_calloc(NAMES_MAX, sizeof(*names), MALLOC_CAP_SPIRAM);
    }
    size_t count = names ? hub_tv_apps(names, NAMES_MAX) : 0;

    /* The most watched first */
    int order[APPS_MAX];
    for (int i = 0; i < t->apps; i++) {
        order[i] = i;
        for (int j = i; j > 0 && t->app[order[j]].minutes > t->app[order[j - 1]].minutes; j--) {
            int k = order[j];
            order[j] = order[j - 1];
            order[j - 1] = k;
        }
    }
    int top = t->apps ? t->app[order[0]].minutes : 0;
    for (int r = 0; r < ROWS; r++) {
        bool on = r < t->apps;
        lv_obj_set_hidden(s_rows[r], !on);
        if (!on) {
            continue;
        }
        const app_use_t *u = &t->app[order[r]];
        char text[HUB_APP_NAME_MAX > HUB_APP_ID_MAX ? HUB_APP_NAME_MAX : HUB_APP_ID_MAX];
        app_name(u->app, names, count, text, sizeof(text));
        lv_label_set_text(s_row_name[r], text);
        fmt_minutes(text, sizeof(text), u->minutes);
        lv_label_set_text(s_row_value[r], text);
        set_bar(s_row_bar[r], u->minutes, top, COL_ACCENT);
    }
    ui_status_set(s_apps_status, t->apps ? UI_STATUS_NONE : UI_STATUS_EMPTY, "No screen time yet today", NULL);
}

static void show(void *arg)
{
    static today_t t;
    taskENTER_CRITICAL(&s_mux);
    t = s;
    taskEXIT_CRITICAL(&s_mux);
    int limit = setting("limit");
    int allowed = limit > 0 ? limit + t.extra : 0;
    int left = allowed - t.used;
    lv_color_t color = limit > 0 && left <= 0 ? COL_BAD : COL_ACCENT;

    char used[24], left_text[24], allowed_text[24], detail[64];
    fmt_minutes(used, sizeof(used), t.used);
    fmt_minutes(left_text, sizeof(left_text), left > 0 ? left : 0);
    fmt_minutes(allowed_text, sizeof(allowed_text), allowed);

    if (s_card_value) {
        lv_label_set_text(s_card_value, used);
        if (!limit) {
            lv_label_set_text(s_card_detail, "Today");
        } else if (left > 0) {
            lv_label_set_text_fmt(s_card_detail, "%s left", left_text);
        } else {
            lv_label_set_text(s_card_detail, "Time's up");
        }
        lv_obj_set_hidden(s_card_bar, !limit);
        set_bar(s_card_bar, t.used, allowed, color);
    }
    if (s_view_detail) {
        int h = t.used / 60, m = t.used % 60;
        lv_label_set_text_fmt(s_view_h, "%d", h);
        lv_label_set_text_fmt(s_view_m, "%d", m);
        lv_obj_set_hidden(s_view_h, !h);
        lv_obj_set_hidden(s_view_h_unit, !h);
        lv_obj_set_hidden(s_view_m, h && !m);
        lv_obj_set_hidden(s_view_m_unit, h && !m);
        lv_obj_set_style_margin_left(s_view_m, h ? UI_S(12) : 0, 0); /* a gap after "h" only */
        if (!limit) {
            snprintf(detail, sizeof(detail), "No daily limit");
        } else if (left > 0) {
            snprintf(detail, sizeof(detail), "%s left of %s", left_text, allowed_text);
        } else {
            snprintf(detail, sizeof(detail), "Daily limit of %s reached", allowed_text);
        }
        lv_label_set_text(s_view_detail, detail);
        lv_obj_set_hidden(s_view_bar, !limit);
        set_bar(s_view_bar, t.used, allowed, color);
        lv_obj_set_hidden(s_view_extra, !limit);
        show_apps(&t);
    }
}

static void refresh(void)
{
    hub_ui_async(show, NULL);
    hub_glance_changed();
}

static void on_extra(lv_event_t *e)
{
    taskENTER_CRITICAL(&s_mux);
    s.extra += EXTRA_MIN;
    int extra = s.extra;
    taskEXIT_CRITICAL(&s_mux);
    hub_kv_set_i32(MODULE_ID, "extra", extra);
    ESP_LOGI(TAG, "%d min more today", EXTRA_MIN);
    check();
    show(NULL);
    hub_glance_changed();
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
    ui_label(head, &font_icons_24, COL_ACCENT_TEXT, ICON_HOURGLASS);
    ui_label(head, &font_sora_16sb, COL_TEXT, "Screen time");
    lv_obj_t *spacer = ui_box(card);
    lv_obj_set_flex_grow(spacer, 1);
    s_card_value = ui_label(card, &font_sora_26m, COL_TEXT, "");
    s_card_detail = ui_label(card, &font_sora_13m, COL_TEXT2, "");
    s_card_bar = bar(card, UI_S(8));
    lv_obj_set_style_margin_top(s_card_bar, UI_S(6), 0);
    show(NULL);
    return card;
}

static lv_obj_t *create_view(lv_obj_t *parent)
{
    lv_obj_t *v = ui_box(parent);
    lv_obj_set_size(v, HUB_VIEW_W, HUB_VIEW_H);
    ui_flex(v, LV_FLEX_FLOW_ROW, UI_GAP);
    int32_t today_w = (HUB_VIEW_W - UI_GAP) * 11 / 20;

    /* Today: the time, the limit, + 15 min */
    lv_obj_t *p = ui_panel(v, today_w, HUB_VIEW_H);
    lv_obj_set_style_pad_all(p, UI_S(36), 0);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(16));
    ui_label(p, &font_sora_16sb, COL_TEXT2, "Today");
    lv_obj_t *now = ui_box(p);
    lv_obj_set_size(now, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(now, LV_FLEX_FLOW_ROW, UI_S(10));
    lv_obj_set_flex_align(now, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_view_h = ui_label(now, &font_sora_88n, COL_TEXT, "");
    s_view_h_unit = ui_label(now, &font_sora_24m, COL_TEXT2, "h");
    s_view_m = ui_label(now, &font_sora_88n, COL_TEXT, "");
    s_view_m_unit = ui_label(now, &font_sora_24m, COL_TEXT2, "min");
    lv_obj_set_style_pad_bottom(s_view_h_unit, UI_S(14), 0);
    lv_obj_set_style_pad_bottom(s_view_m_unit, UI_S(14), 0);
    s_view_detail = ui_label(p, &font_sora_15m, COL_TEXT2, "");
    s_view_bar = bar(p, UI_S(12));
    lv_obj_t *spacer = ui_box(p);
    lv_obj_set_flex_grow(spacer, 1);
    s_view_extra = ui_btn_icon(p, UI_S(200), UI_S(56), 16, COL_SURFACE2, &font_icons_20, ICON_PLUS, COL_TEXT,
                               "15 min", false);
    lv_obj_add_event_cb(s_view_extra, on_extra, LV_EVENT_CLICKED, NULL);

    /* The apps, most watched first */
    lv_obj_t *a = ui_panel(v, HUB_VIEW_W - UI_GAP - today_w, HUB_VIEW_H);
    lv_obj_set_style_pad_all(a, UI_S(28), 0);
    ui_flex(a, LV_FLEX_FLOW_COLUMN, UI_S(18));
    ui_label(a, &font_sora_16sb, COL_TEXT2, "Apps");
    for (int r = 0; r < ROWS; r++) {
        lv_obj_t *row = ui_box(a);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        ui_flex(row, LV_FLEX_FLOW_COLUMN, UI_S(8));
        lv_obj_t *line = ui_box(row);
        lv_obj_set_size(line, LV_PCT(100), LV_SIZE_CONTENT);
        ui_flex(line, LV_FLEX_FLOW_ROW, UI_S(12));
        s_row_name[r] = ui_label(line, &font_sora_15m, COL_TEXT, "");
        lv_label_set_long_mode(s_row_name[r], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_flex_grow(s_row_name[r], 1);
        s_row_value[r] = ui_label(line, &font_sora_15m, COL_TEXT2, "");
        s_row_bar[r] = bar(row, UI_S(6));
        s_rows[r] = row;
    }
    s_apps_status = ui_status_create(a, LV_PCT(100), UI_S(120), false);
    lv_obj_set_flex_grow(s_apps_status, 1);
    show(NULL);
    return v;
}

static void on_show(void)
{
    show(NULL);
}

/* Near the limit: the minutes left, then "Time's up". */
static bool glance(hub_glance_t *out)
{
    int left = minutes_left();
    int warn = setting("warn");
    if (left == INT_MAX || left > warn) {
        return false;
    }
    out->icon = ICON_HOURGLASS;
    out->color = left > 0 ? COL_ACCENT_TEXT : COL_BAD;
    if (left > 0) {
        snprintf(out->text, sizeof(out->text), "%d min", left);
    } else {
        snprintf(out->text, sizeof(out->text), "Time's up");
    }
    return true;
}

/* --- Module (controller task) ----------------------------------------------------- */

static void init(void)
{
    const esp_timer_create_args_t args = {.callback = on_off_timer, .name = "screen_time"};
    if (esp_timer_create(&args, &s_off_timer) != ESP_OK) {
        ESP_LOGE(TAG, "no timer");
    }
    load();
    roll_day();
    ESP_LOGI(TAG, "today %d min", s.used);
}

static void event(hub_event_t ev)
{
    switch (ev) {
    case HUB_EV_MINUTE:
        roll_day();
        count_minute();
        check();
        break;
    case HUB_EV_TV:
        tv_changed();
        break;
    case HUB_EV_SCREEN_ON:
        refresh();
        break;
    case HUB_EV_NETWORK_UP:
    case HUB_EV_NETWORK_DOWN:
    case HUB_EV_SCREEN_OFF:
    case HUB_EV_NOW_PLAYING:
        break;
    }
}

static void setting_changed(const char *key)
{
    update_hold();
    check();
    refresh();
}

static const hub_module_t s_module = {
    .id = MODULE_ID,
    .name = "Screen time",
    .icon = ICON_HOURGLASS,
    .init = init,
    .event = event,
    .create_view = create_view,
    .show = on_show,
    .create_card = create_card,
    .glance = glance,
    .card_sizes = HUB_CARD_SMALL | HUB_CARD_MEDIUM | HUB_CARD_LARGE,
    .settings = k_settings,
    .settings_count = sizeof(k_settings) / sizeof(k_settings[0]),
    .setting_changed = setting_changed,
};
HUB_MODULE(s_module);
