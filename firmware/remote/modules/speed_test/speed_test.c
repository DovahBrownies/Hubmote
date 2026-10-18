/*
 * Internet speed: the TV's download, upload and latency. The companion app
 * runs the test on the Streamer (companion 0.7+, against Cloudflare's
 * speed test), so it measures what streaming gets, not the remote's own
 * Wi-Fi. The numbers show live while it runs; the results stay, the last
 * HISTORY of them listed by time.
 *
 * Threads: everything runs on the LVGL task. hub_speedtest() is polled by
 * an lv_timer while a test runs (it changes about twice a second).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "hub.h"
#include "ui_kit.h"

#define MODULE_ID  "speed_test"
#define HISTORY    6
#define POLL_MS    500
#define GIVE_UP_US (90 * 1000000LL) /* no end from the TV by then: failed */

static const char *TAG = "speed_test";

typedef struct {
    int64_t at; /* time(NULL) when it finished; 0 if the clock wasn't set */
    float down, up, ping;
} result_t;

static result_t s_hist[HISTORY]; /* newest first */
static int s_count;
static bool s_loaded;
static bool s_running;  /* we started one and haven't seen its end */
static bool s_seen;     /* ... and the TV has said it runs (not an old "done") */
static bool s_failed;   /* the last one didn't finish */
static int64_t s_started_us;
static lv_timer_t *s_poll;

/* Screen and card */
static lv_obj_t *s_down, *s_down_unit, *s_none;
static lv_obj_t *s_up, *s_ping;
static lv_obj_t *s_bar, *s_line, *s_test;
static lv_obj_t *s_rows[HISTORY], *s_row_when[HISTORY], *s_row_down[HISTORY], *s_row_up[HISTORY];
static lv_obj_t *s_hist_status;
static lv_obj_t *s_card_value, *s_card_detail;

/* --- Results, kept in NVS as "at,down,up,ping;..." ----------------------------- */

static void load(void)
{
    char text[HISTORY * 48] = "";
    hub_kv_get_str(MODULE_ID, "hist", text, sizeof(text));
    s_count = 0;
    char *p = text;
    while (*p && s_count < HISTORY) {
        result_t r = {0};
        r.at = strtoll(p, &p, 10);
        r.down = strtof(*p == ',' ? p + 1 : p, &p);
        r.up = strtof(*p == ',' ? p + 1 : p, &p);
        r.ping = strtof(*p == ',' ? p + 1 : p, &p);
        s_hist[s_count++] = r;
        p = strchr(p, ';');
        if (!p) {
            break;
        }
        p++;
    }
}

static void save(void)
{
    char text[HISTORY * 48] = "";
    size_t at = 0;
    for (int i = 0; i < s_count && at < sizeof(text); i++) {
        at += snprintf(text + at, sizeof(text) - at, "%s%lld,%.1f,%.1f,%.1f", i ? ";" : "", (long long)s_hist[i].at,
                       s_hist[i].down, s_hist[i].up, s_hist[i].ping);
    }
    hub_kv_set_str(MODULE_ID, "hist", text);
}

static void on_poll(lv_timer_t *t);

/* The history and the poll timer, the first time they're needed. */
static void ensure(void)
{
    if (!s_loaded) {
        s_loaded = true;
        load();
    }
    if (!s_poll) {
        s_poll = lv_timer_create(on_poll, POLL_MS, NULL);
        lv_timer_pause(s_poll);
    }
}

static void add(const hub_speedtest_t *st)
{
    memmove(&s_hist[1], &s_hist[0], sizeof(result_t) * (HISTORY - 1));
    s_hist[0] = (result_t){.at = hub_time_valid() ? (int64_t)time(NULL) : 0,
                           .down = st->down_mbps,
                           .up = st->up_mbps,
                           .ping = st->ping_ms};
    s_count = s_count < HISTORY ? s_count + 1 : HISTORY;
    save();
}

/* --- Formatting --------------------------------------------------------------------- */

/* 312, or 4.6 under 10 */
static void fmt_mbps(char *out, size_t len, float mbps)
{
    if (mbps < 10) {
        snprintf(out, len, "%.1f", mbps);
    } else {
        snprintf(out, len, "%.0f", mbps);
    }
}

/* "21:40" today, "Oct 16" before, "" without a time */
static void fmt_when(char *out, size_t len, int64_t at)
{
    struct tm then, now;
    out[0] = '\0';
    if (!at || !hub_time_local(&now)) {
        return;
    }
    hub_time_to_local((time_t)at, &then);
    if (then.tm_year == now.tm_year && then.tm_yday == now.tm_yday) {
        strftime(out, len, "%H:%M", &then);
    } else {
        strftime(out, len, "%b %d", &then);
    }
}

static const char *phase(hub_speed_state_t state)
{
    switch (state) {
    case HUB_SPEED_LATENCY: return "Testing latency";
    case HUB_SPEED_DOWNLOAD: return "Testing download";
    case HUB_SPEED_UPLOAD: return "Testing upload";
    default: return "Testing";
    }
}

/* --- Screen and card (LVGL task) ------------------------------------------------- */

static bool running_state(hub_speed_state_t state)
{
    return state == HUB_SPEED_LATENCY || state == HUB_SPEED_DOWNLOAD || state == HUB_SPEED_UPLOAD;
}

static void show(void)
{
    ensure();
    hub_speedtest_t st;
    hub_speedtest(&st);
    if (s_running && !s_seen) { /* asked; the TV hasn't started yet */
        st = (hub_speedtest_t){.state = HUB_SPEED_LATENCY};
    }
    bool live = s_running;
    bool can = st.state != HUB_SPEED_UNAVAILABLE;
    const result_t *last = s_count ? &s_hist[0] : NULL;
    float down = live ? st.down_mbps : last ? last->down : 0;
    float up = live ? st.up_mbps : last ? last->up : 0;
    float ping = live ? st.ping_ms : last ? last->ping : 0;
    bool known = live || last;
    lv_color_t color = live ? COL_TEXT2 : COL_TEXT;

    char num[16], when[16], line[48], line2[40];
    fmt_when(when, sizeof(when), last ? last->at : 0);
    if (live) {
        snprintf(line, sizeof(line), "%s", phase(st.state));
    } else if (s_failed) {
        snprintf(line, sizeof(line), "Couldn't test");
    } else if (!can) {
        snprintf(line, sizeof(line), "Needs the companion app 0.7 on the TV");
    } else if (last && when[0]) {
        snprintf(line, sizeof(line), "Tested %s", when);
    } else {
        line[0] = '\0';
    }

    if (s_down) {
        snprintf(num, sizeof(num), "%.0f", down);
        lv_label_set_text(s_down, num);
        lv_obj_set_style_text_color(s_down, color, 0);
        lv_obj_set_hidden(s_down, !known);
        lv_obj_set_hidden(s_down_unit, !known);
        lv_obj_set_hidden(s_none, known);
        fmt_mbps(num, sizeof(num), up);
        if (known) {
            lv_label_set_text_fmt(s_up, "%s Mbit/s", num);
            snprintf(line2, sizeof(line2), "%.0f ms", ping); /* LVGL's formatting has no floats */
            lv_label_set_text(s_ping, line2);
        } else {
            lv_label_set_text(s_up, "-");
            lv_label_set_text(s_ping, "-");
        }
        lv_obj_set_hidden(s_bar, !live);
        lv_bar_set_value(s_bar, st.progress, LV_ANIM_ON);
        lv_label_set_text(s_line, line);
        lv_obj_set_state(s_test, LV_STATE_DISABLED, live || !can);
        for (int i = 0; i < HISTORY; i++) {
            bool on = i < s_count;
            lv_obj_set_hidden(s_rows[i], !on);
            if (on) {
                fmt_when(when, sizeof(when), s_hist[i].at);
                lv_label_set_text(s_row_when[i], when[0] ? when : "-");
                fmt_mbps(num, sizeof(num), s_hist[i].down);
                lv_label_set_text(s_row_down[i], num);
                fmt_mbps(num, sizeof(num), s_hist[i].up);
                lv_label_set_text(s_row_up[i], num);
            }
        }
        ui_status_set(s_hist_status, s_count ? UI_STATUS_NONE : UI_STATUS_EMPTY, "No tests yet", NULL);
    }
    if (s_card_value) {
        if (known) {
            fmt_mbps(num, sizeof(num), down);
            lv_label_set_text_fmt(s_card_value, "%s Mbit/s", num);
            fmt_mbps(num, sizeof(num), up);
            if (live) {
                lv_label_set_text(s_card_detail, phase(st.state));
            } else {
                snprintf(line2, sizeof(line2), "%s up, %.0f ms", num, ping);
                lv_label_set_text(s_card_detail, line2);
            }
        } else {
            lv_label_set_text(s_card_value, "Not tested");
            lv_label_set_text(s_card_detail, can ? "" : "Needs the companion app");
        }
        lv_obj_set_style_text_color(s_card_value, color, 0);
    }
}

static void on_poll(lv_timer_t *t)
{
    hub_speedtest_t st;
    hub_speedtest(&st);
    s_seen |= running_state(st.state);
    /* Done or failed counts once the TV has said it runs: until then they
     * are the last test's. */
    bool ended = (s_seen && (st.state == HUB_SPEED_DONE || st.state == HUB_SPEED_FAILED)) ||
                 st.state == HUB_SPEED_UNAVAILABLE;
    bool waited = esp_timer_get_time() - s_started_us > GIVE_UP_US;
    if (s_running && (ended || waited)) {
        s_running = false;
        s_failed = st.state != HUB_SPEED_DONE;
        if (!s_failed) {
            add(&st);
            ESP_LOGI(TAG, "%.0f ms, %.1f down, %.1f up", st.ping_ms, st.down_mbps, st.up_mbps);
        } else {
            ESP_LOGW(TAG, "the test didn't finish");
        }
    }
    show();
    if (!s_running) {
        lv_timer_pause(t);
    }
}

static void on_test(lv_event_t *e)
{
    ensure();
    if (s_running || hub_tv_speedtest() != ESP_OK) {
        return;
    }
    s_running = true;
    s_seen = false;
    s_failed = false;
    s_started_us = esp_timer_get_time();
    lv_timer_reset(s_poll);
    lv_timer_resume(s_poll);
    show();
}

/* A result: label above, value below. */
static lv_obj_t *figure(lv_obj_t *parent, const char *icon, const char *label)
{
    lv_obj_t *box = ui_box(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(box, LV_FLEX_FLOW_COLUMN, UI_S(4));
    lv_obj_t *head = ui_box(box);
    lv_obj_set_size(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(6));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (icon) {
        ui_label(head, &font_icons_16, COL_TEXT2, icon);
    }
    ui_label(head, &font_sora_15m, COL_TEXT2, label);
    return ui_label(box, &font_sora_24m, COL_TEXT, "");
}

static lv_obj_t *create_view(lv_obj_t *parent)
{
    lv_obj_t *v = ui_box(parent);
    lv_obj_set_size(v, HUB_VIEW_W, HUB_VIEW_H);
    ui_flex(v, LV_FLEX_FLOW_ROW, UI_GAP);
    int32_t main_w = (HUB_VIEW_W - UI_GAP) * 3 / 5;

    /* The latest: download big, then upload and latency, then the test */
    lv_obj_t *p = ui_panel(v, main_w, HUB_VIEW_H);
    lv_obj_set_style_pad_all(p, UI_S(36), 0);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(14));
    lv_obj_t *head = ui_box(p);
    lv_obj_set_size(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(6));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, &font_icons_16, COL_TEXT2, ICON_ARROW_DOWN);
    ui_label(head, &font_sora_16sb, COL_TEXT2, "Download");
    lv_obj_t *big = ui_box(p);
    lv_obj_set_size(big, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(big, LV_FLEX_FLOW_ROW, UI_S(10));
    lv_obj_set_flex_align(big, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_down = ui_label(big, &font_sora_88n, COL_TEXT, "");
    s_down_unit = ui_label(big, &font_sora_24m, COL_TEXT2, "Mbit/s");
    lv_obj_set_style_pad_bottom(s_down_unit, UI_S(14), 0);
    s_none = ui_label(big, &font_sora_26m, COL_TEXT2, "Not tested yet");
    lv_obj_t *row = ui_box(p);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(56));
    s_up = figure(row, ICON_ARROW_UP, "Upload");
    s_ping = figure(row, NULL, "Latency");
    lv_obj_t *spacer = ui_box(p);
    lv_obj_set_flex_grow(spacer, 1);
    s_bar = lv_bar_create(p);
    lv_obj_set_size(s_bar, LV_PCT(100), UI_S(8));
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, COL_SURFACE3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, UI_S(4), LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, UI_S(4), LV_PART_INDICATOR);
    lv_obj_set_clickable(s_bar, false);
    s_line = ui_label(p, &font_sora_15m, COL_TEXT2, "");
    s_test = ui_btn_icon(p, UI_S(220), UI_S(56), 16, COL_ACCENT, &font_icons_20, ICON_GAUGE, COL_ON_ACCENT,
                         "Test now", false);
    lv_obj_add_event_cb(s_test, on_test, LV_EVENT_CLICKED, NULL);

    /* Recent results */
    lv_obj_t *h = ui_panel(v, HUB_VIEW_W - UI_GAP - main_w, HUB_VIEW_H);
    lv_obj_set_style_pad_all(h, UI_S(28), 0);
    ui_flex(h, LV_FLEX_FLOW_COLUMN, UI_S(14));
    ui_label(h, &font_sora_16sb, COL_TEXT2, "Recent");
    for (int i = 0; i < HISTORY; i++) {
        lv_obj_t *r = s_rows[i] = ui_box(h);
        lv_obj_set_size(r, LV_PCT(100), UI_S(44));
        ui_flex(r, LV_FLEX_FLOW_ROW, UI_S(8));
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_row_when[i] = ui_label(r, &font_sora_15m, COL_TEXT2, "");
        lv_obj_set_flex_grow(s_row_when[i], 1);
        ui_label(r, &font_icons_16, COL_TEXT3, ICON_ARROW_DOWN);
        s_row_down[i] = ui_label(r, &font_sora_15m, COL_TEXT, "");
        lv_obj_set_style_min_width(s_row_down[i], UI_S(52), 0);
        ui_label(r, &font_icons_16, COL_TEXT3, ICON_ARROW_UP);
        s_row_up[i] = ui_label(r, &font_sora_15m, COL_TEXT, "");
        lv_obj_set_style_min_width(s_row_up[i], UI_S(44), 0);
    }
    s_hist_status = ui_status_create(h, LV_PCT(100), UI_S(120), false);
    lv_obj_set_flex_grow(s_hist_status, 1);
    show();
    return v;
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
    ui_label(head, &font_icons_24, COL_ACCENT_TEXT, ICON_GAUGE);
    ui_label(head, &font_sora_16sb, COL_TEXT, "Internet speed");
    lv_obj_t *spacer = ui_box(card);
    lv_obj_set_flex_grow(spacer, 1);
    s_card_value = ui_label(card, &font_sora_26m, COL_TEXT, "");
    s_card_detail = ui_label(card, &font_sora_13m, COL_TEXT2, "");
    show();
    return card;
}

static void on_show(void)
{
    show();
}

/* --- Module (controller task) ----------------------------------------------------- */

static void event(hub_event_t ev)
{
    switch (ev) {
    case HUB_EV_NETWORK_UP:
    case HUB_EV_NETWORK_DOWN:
    case HUB_EV_SCREEN_ON:
    case HUB_EV_SCREEN_OFF:
    case HUB_EV_MINUTE:
    case HUB_EV_NOW_PLAYING:
    case HUB_EV_TV:
        break;
    }
}

static const hub_module_t s_module = {
    .id = MODULE_ID,
    .name = "Internet speed",
    .icon = ICON_GAUGE,
    .event = event,
    .create_view = create_view,
    .show = on_show,
    .create_card = create_card,
    .card_sizes = HUB_CARD_SMALL | HUB_CARD_MEDIUM | HUB_CARD_LARGE,
};
HUB_MODULE(s_module);
