/*
 * Weather screens: the full screen (now on the left; the next 24 hours and
 * 7 days on the right), the Home tab card, the top-bar glance, and the
 * location search. Everything here runs on the LVGL task.
 */
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "ui_kit.h"
#include "weather.h"

#define HERO_W     UI_S(360)
#define HOURLY_H   UI_S(196)
#define HOUR_COL_W UI_S(64)
#define CARD_HOURS 6
#define CARD_HOUR_W UI_S(46) /* one hour on the card: fits "-12°" */

static weather_state_t *s_snap; /* PSRAM copy used for drawing */

static weather_state_t *snap(void)
{
    if (!s_snap) {
        s_snap = heap_caps_calloc(1, sizeof(*s_snap), MALLOC_CAP_SPIRAM);
    }
    return s_snap;
}

/* Full screen */
static lv_obj_t *s_body;
static lv_obj_t *s_status; /* set location, loading, couldn't load */
static lv_obj_t *s_place;
static lv_obj_t *s_updated;
static lv_obj_t *s_refreshing;
static lv_obj_t *s_unit_c, *s_unit_f;
static lv_obj_t *s_now_icon, *s_now_temp, *s_now_text, *s_now_sub;
static lv_obj_t *s_tile_value[4];
static lv_obj_t *s_hour_col[WX_HOURS], *s_hour_time[WX_HOURS], *s_hour_icon[WX_HOURS], *s_hour_temp[WX_HOURS],
    *s_hour_pop[WX_HOURS];
static lv_obj_t *s_day_row[WX_DAYS], *s_day_name[WX_DAYS], *s_day_icon[WX_DAYS], *s_day_pop[WX_DAYS],
    *s_day_lo[WX_DAYS], *s_day_hi[WX_DAYS], *s_day_fill[WX_DAYS], *s_day_dot[WX_DAYS];
static lv_obj_t *s_results;
static lv_obj_t *s_results_list;
static lv_obj_t *s_results_status;

/* Home card */
static lv_obj_t *s_card_body;
static lv_obj_t *s_card_status;
static lv_obj_t *s_c_place, *s_c_spin, *s_c_icon, *s_c_temp, *s_c_text, *s_c_hl;
static lv_obj_t *s_c_time[CARD_HOURS], *s_c_hicon[CARD_HOURS], *s_c_htemp[CARD_HOURS];
static lv_obj_t *s_c_hour[CARD_HOURS];

/* --- Formatting -------------------------------------------------------------- */

static const char *look_icon(wx_look_t look)
{
    switch (look) {
    case WX_SUN: return ICON_W_SUN;
    case WX_MOON: return ICON_W_MOON;
    case WX_CLOUD_SUN: return ICON_W_CLOUD_SUN;
    case WX_CLOUD_MOON: return ICON_W_CLOUD_MOON;
    case WX_FOG: return ICON_W_FOG;
    case WX_DRIZZLE: return ICON_W_DRIZZLE;
    case WX_RAIN: return ICON_W_RAIN;
    case WX_HEAVY_RAIN: return ICON_W_HEAVY_RAIN;
    case WX_SHOWERS_DAY: return ICON_W_SUN_RAIN;
    case WX_SHOWERS_NIGHT: return ICON_W_MOON_RAIN;
    case WX_SNOW: return ICON_W_SNOW;
    case WX_HAIL: return ICON_W_HAIL;
    case WX_STORM: return ICON_W_STORM;
    case WX_CLOUDY:
    default: return ICON_W_CLOUDY;
    }
}

static lv_color_t look_color(wx_look_t look)
{
    switch (look) {
    case WX_SUN: return COL_SUN;
    case WX_CLOUD_SUN: return COL_SUN_SOFT;
    case WX_MOON:
    case WX_CLOUD_MOON: return COL_MOON;
    case WX_DRIZZLE:
    case WX_RAIN:
    case WX_HEAVY_RAIN:
    case WX_SHOWERS_DAY:
    case WX_SHOWERS_NIGHT: return COL_RAIN;
    case WX_SNOW: return COL_TEXT;
    case WX_HAIL:
    case WX_STORM: return COL_STORM;
    default: return COL_TEXT2;
    }
}

static void set_look(lv_obj_t *icon, int code, bool day)
{
    wx_look_t look = wx_code_look(code, day);
    lv_label_set_text(icon, look_icon(look));
    lv_obj_set_style_text_color(icon, look_color(look), 0);
}

/* Data is metric; shown in the chosen units. */
static long temp(float c)
{
    return lroundf(s_snap->imperial ? wx_c_to_f(c) : c);
}

static void set_temp(lv_obj_t *label, float c)
{
    lv_label_set_text_fmt(label, "%ld\xC2\xB0", temp(c));
}

/* Local time of the forecast's place. */
static void local_tm(int64_t t, struct tm *out)
{
    time_t local = (time_t)(t + s_snap->data.utc_offset);
    gmtime_r(&local, out);
}

static void hhmm(char *out, size_t len, int64_t t)
{
    struct tm tm;
    local_tm(t, &tm);
    snprintf(out, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static const char *uv_word(float uv)
{
    return uv < 3 ? "Low" : uv < 6 ? "Moderate" : uv < 8 ? "High" : uv < 11 ? "Very high" : "Extreme";
}

/* --- Location search --------------------------------------------------------- */

static void back_to_weather(void)
{
    ui_module_open(WEATHER_ID);
}

/* The results list, or a status (searching, failed, nothing found) in its place. */
static void show_results_status(ui_status_t state, const char *title)
{
    ui_status_set(s_results_status, state, title, NULL);
    lv_obj_set_hidden(s_results_list, state != UI_STATUS_NONE);
}

static void on_city(const char *text)
{
    back_to_weather();
    if (!text || !text[0]) {
        return;
    }
    lv_obj_set_hidden(s_results, false);
    show_results_status(UI_STATUS_LOADING, NULL);
    weather_search(text);
}

static void on_city_cancel(void)
{
    back_to_weather();
}

static void open_search(lv_event_t *e)
{
    ui_kb_open_local(s_snap->has_place ? s_snap->place.name : "", "City", 40, on_city, on_city_cancel);
}

static wx_place_t s_choices[WX_PLACES_MAX];

static void on_choice(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_set_hidden(s_results, true);
    weather_set_place(&s_choices[i]);
}

static void on_results_close(lv_event_t *e)
{
    lv_obj_set_hidden(s_results, true);
}

void weather_ui_search_results(const wx_place_t *places, int count, bool failed)
{
    if (!s_results) {
        return;
    }
    if (failed) {
        show_results_status(UI_STATUS_ERROR, "Search failed");
        return;
    }
    if (count == 0) {
        show_results_status(UI_STATUS_EMPTY, "No places found");
        return;
    }
    show_results_status(UI_STATUS_NONE, NULL);
    lv_obj_clean(s_results_list);
    for (int i = 0; i < count && i < WX_PLACES_MAX; i++) {
        s_choices[i] = places[i];
        lv_obj_t *row = ui_btn(s_results_list, LV_PCT(100), UI_S(60), 14, COL_SURFACE2);
        lv_obj_set_style_pad_hor(row, UI_S(16), 0);
        ui_flex(row, LV_FLEX_FLOW_COLUMN, UI_S(2));
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, on_choice, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_label(row, &font_sora_16sb, COL_TEXT, places[i].name);
        ui_label(row, &font_sora_12, COL_TEXT2, places[i].region);
    }
}

static void build_results(lv_obj_t *parent)
{
    s_results = ui_panel(parent, HUB_VIEW_W, HUB_VIEW_H);
    lv_obj_set_pos(s_results, 0, 0);
    ui_flex(s_results, LV_FLEX_FLOW_COLUMN, UI_S(12));
    lv_obj_t *head = ui_box(s_results);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, &font_sora_20sb, COL_TEXT, "Choose a place");
    lv_obj_t *close = ui_btn_icon(head, UI_S(44), UI_S(44), 12, COL_SURFACE2, &font_icons_20, ICON_CLOSE, COL_TEXT, NULL, false);
    lv_obj_add_event_cb(close, on_results_close, LV_EVENT_CLICKED, NULL);
    s_results_list = ui_box(s_results);
    lv_obj_set_width(s_results_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_results_list, 1);
    lv_obj_set_scrollable(s_results_list, true);
    lv_obj_set_scroll_dir(s_results_list, LV_DIR_VER);
    ui_flex(s_results_list, LV_FLEX_FLOW_COLUMN, UI_S(8));
    s_results_status = ui_status_create(s_results, LV_PCT(100), LV_SIZE_CONTENT, false);
    lv_obj_set_flex_grow(s_results_status, 1);
    ui_status_set_icon(s_results_status, UI_STATUS_EMPTY, &font_icons_28, ICON_SEARCH, COL_TEXT3);
    ui_status_add_action(s_results_status, UI_STATUS_ERROR, ICON_SEARCH, "Search again", true, open_search, NULL);
    ui_status_add_action(s_results_status, UI_STATUS_EMPTY, ICON_SEARCH, "Search again", true, open_search, NULL);
    lv_obj_set_hidden(s_results, true);
}

/* --- Full screen ------------------------------------------------------------- */

static void on_unit(lv_event_t *e)
{
    weather_set_imperial(lv_event_get_user_data(e) != NULL);
}

static lv_obj_t *unit_btn(lv_obj_t *parent, const char *text, bool imperial)
{
    lv_obj_t *b = ui_btn_text(parent, UI_S(44), UI_S(32), 10, COL_SURFACE1, &font_sora_13m, COL_TEXT2, text);
    lv_obj_add_event_cb(b, on_unit, LV_EVENT_CLICKED, imperial ? (void *)1 : NULL);
    return b;
}

static lv_obj_t *tile(lv_obj_t *parent, const char *icon, const char *label, int i)
{
    lv_obj_t *t = ui_box(parent);
    lv_obj_set_style_bg_color(t, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, ui_radius(14), 0);
    lv_obj_set_style_pad_all(t, UI_S(12), 0);
    lv_obj_set_size(t, LV_PCT(48), LV_SIZE_CONTENT);
    ui_flex(t, LV_FLEX_FLOW_COLUMN, UI_S(6));
    lv_obj_t *head = ui_box(t);
    lv_obj_set_size(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(6));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, &font_icons_16, COL_TEXT2, icon);
    ui_label(head, &font_sora_12, COL_TEXT2, label);
    s_tile_value[i] = ui_label(t, &font_sora_16sb, COL_TEXT, "--");
    return t;
}

static void build_hero(lv_obj_t *parent)
{
    lv_obj_t *hero = ui_panel(parent, HERO_W, HUB_VIEW_H);
    lv_obj_set_style_pad_all(hero, UI_S(22), 0);
    ui_flex(hero, LV_FLEX_FLOW_COLUMN, UI_S(10));

    lv_obj_t *head = ui_box(hero);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *place = ui_btn(head, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE1);
    lv_obj_set_flex_grow(place, 1);
    ui_flex(place, LV_FLEX_FLOW_ROW, UI_S(6));
    lv_obj_set_flex_align(place, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(place, open_search, LV_EVENT_CLICKED, NULL);
    ui_label(place, &font_icons_16, COL_TEXT2, ICON_MAP_PIN);
    s_place = ui_label(place, &font_sora_20sb, COL_TEXT, "");
    lv_label_set_long_mode(s_place, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_place, 1);
    lv_obj_t *units = ui_box(head);
    lv_obj_set_size(units, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(units, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(units, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(units, ui_radius(12), 0);
    lv_obj_set_style_pad_all(units, UI_S(2), 0);
    ui_flex(units, LV_FLEX_FLOW_ROW, UI_S(2));
    s_unit_c = unit_btn(units, "\xC2\xB0" "C", false);
    s_unit_f = unit_btn(units, "\xC2\xB0" "F", true);

    lv_obj_t *updated = ui_box(hero);
    lv_obj_set_size(updated, LV_SIZE_CONTENT, UI_S(16));
    ui_flex(updated, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(updated, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_updated = ui_label(updated, &font_mono_12, COL_TEXT2, "");
    s_refreshing = ui_spinner(updated, UI_S(14), COL_TEXT2);
    ui_spinner_run(s_refreshing, false);

    lv_obj_t *now = ui_box(hero);
    lv_obj_set_size(now, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(now, UI_S(6), 0);
    ui_flex(now, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(now, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_now_icon = ui_label(now, &font_weather_96, COL_SUN, ICON_W_CLOUD_SUN);
    s_now_temp = ui_label(now, &font_sora_88n, COL_TEXT, "--");

    s_now_text = ui_label(hero, &font_sora_20sb, COL_TEXT, "");
    s_now_sub = ui_label(hero, &font_sora_13m, COL_TEXT2, "");

    lv_obj_t *spacer = ui_box(hero);
    lv_obj_set_width(spacer, UI_S(1));
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t *tiles = ui_box(hero);
    lv_obj_set_size(tiles, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(tiles, UI_S(8), 0);
    tile(tiles, ICON_W_WIND, "Wind", 0);
    tile(tiles, ICON_W_DROPLET, "Humidity", 1);
    tile(tiles, ICON_W_SUN, "UV index", 2);
    tile(tiles, ICON_W_SUNRISE, "Sun", 3);

    ui_label(hero, &font_sora_11, COL_TEXT3, "Weather data by Open-Meteo.com");
}

static void build_hourly(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, LV_PCT(100), HOURLY_H);
    lv_obj_set_style_pad_all(p, UI_S(18), 0);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(10));
    ui_label(p, &font_sora_13m, COL_TEXT2, "Next 24 hours");
    lv_obj_t *row = ui_box(p);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_scrollable(row, true);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(4));
    for (int i = 0; i < WX_HOURS; i++) {
        lv_obj_t *c = s_hour_col[i] = ui_box(row);
        lv_obj_set_size(c, HOUR_COL_W, LV_PCT(100));
        lv_obj_set_style_radius(c, ui_radius(14), 0);
        lv_obj_set_style_bg_color(c, COL_SURFACE2, 0);
        lv_obj_set_style_bg_opa(c, i == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_ver(c, UI_S(8), 0);
        ui_flex(c, LV_FLEX_FLOW_COLUMN, 0);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_hour_time[i] = ui_label(c, &font_mono_12, COL_TEXT2, "");
        s_hour_icon[i] = ui_label(c, &font_icons_28, COL_TEXT2, "");
        s_hour_temp[i] = ui_label(c, &font_sora_16sb, COL_TEXT, "");
        s_hour_pop[i] = ui_label(c, &font_mono_11, COL_TEXT3, "");
    }
}

static void build_daily(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, LV_PCT(100), HUB_VIEW_H - HOURLY_H - UI_GAP);
    lv_obj_set_style_pad_all(p, UI_S(18), 0);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(2));
    lv_obj_t *title = ui_label(p, &font_sora_13m, COL_TEXT2, "7 days");
    lv_obj_set_style_pad_bottom(title, UI_S(6), 0);
    for (int i = 0; i < WX_DAYS; i++) {
        lv_obj_t *r = s_day_row[i] = ui_box(p);
        lv_obj_set_size(r, LV_PCT(100), UI_S(36));
        ui_flex(r, LV_FLEX_FLOW_ROW, UI_S(10));
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_day_name[i] = ui_label(r, &font_sora_15m, COL_TEXT, "");
        lv_obj_set_width(s_day_name[i], UI_S(64));
        s_day_icon[i] = ui_label(r, &font_icons_24, COL_TEXT2, "");
        lv_obj_set_width(s_day_icon[i], UI_S(30));
        s_day_pop[i] = ui_label(r, &font_mono_12, COL_TEXT3, "");
        lv_obj_set_width(s_day_pop[i], UI_S(44));
        s_day_lo[i] = ui_label(r, &font_sora_15m, COL_TEXT2, "");
        lv_obj_set_width(s_day_lo[i], UI_S(40));
        lv_obj_set_style_text_align(s_day_lo[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_t *track = ui_box(r);
        lv_obj_set_height(track, UI_S(6));
        lv_obj_set_flex_grow(track, 1);
        lv_obj_set_style_radius(track, ui_radius(3), 0);
        lv_obj_set_style_bg_color(track, COL_SURFACE3, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_overflow_visible(track, true);
        s_day_fill[i] = ui_box(track);
        lv_obj_set_height(s_day_fill[i], UI_S(6));
        lv_obj_set_style_radius(s_day_fill[i], ui_radius(3), 0);
        lv_obj_set_style_bg_color(s_day_fill[i], COL_COOL, 0);
        lv_obj_set_style_bg_grad_color(s_day_fill[i], COL_WARM, 0);
        lv_obj_set_style_bg_grad_dir(s_day_fill[i], LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(s_day_fill[i], LV_OPA_COVER, 0);
        s_day_dot[i] = ui_box(track);
        lv_obj_set_size(s_day_dot[i], UI_S(12), UI_S(12));
        lv_obj_set_y(s_day_dot[i], -UI_S(3));
        lv_obj_set_style_translate_x(s_day_dot[i], -UI_S(6), 0);
        lv_obj_set_style_radius(s_day_dot[i], ui_radius(LV_RADIUS_CIRCLE), 0);
        lv_obj_set_style_bg_color(s_day_dot[i], COL_TEXT, 0);
        lv_obj_set_style_bg_opa(s_day_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_day_dot[i], UI_S(2), 0);
        lv_obj_set_style_border_color(s_day_dot[i], COL_SURFACE1, 0);
        lv_obj_set_hidden(s_day_dot[i], true);
        s_day_hi[i] = ui_label(r, &font_sora_15m, COL_TEXT, "");
        lv_obj_set_width(s_day_hi[i], UI_S(40));
    }
}

static void on_retry(lv_event_t *e)
{
    weather_retry();
}

static void build_status(lv_obj_t *parent)
{
    s_status = ui_status_create(parent, HUB_VIEW_W, HUB_VIEW_H, false);
    lv_obj_set_pos(s_status, 0, 0);
    ui_status_set_icon(s_status, UI_STATUS_EMPTY, &font_weather_96, ICON_W_CLOUD_SUN, COL_SUN_SOFT);
    ui_status_add_action(s_status, UI_STATUS_EMPTY, ICON_MAP_PIN, "Set location", true, open_search, NULL);
    ui_status_add_action(s_status, UI_STATUS_ERROR, ICON_REFRESH, "Try again", true, on_retry, NULL);
    ui_status_add_action(s_status, UI_STATUS_ERROR, ICON_MAP_PIN, "Change place", false, open_search, NULL);
}

/* What covers the screen or card instead of the forecast, if anything. */
static ui_status_t status_of(const weather_state_t *s)
{
    if (!s->has_place) {
        return UI_STATUS_EMPTY;
    }
    if (s->data.valid) {
        return UI_STATUS_NONE;
    }
    return s->failed && !s->fetching ? UI_STATUS_ERROR : UI_STATUS_LOADING;
}

lv_obj_t *weather_ui_create_view(lv_obj_t *parent)
{
    if (!s_snap) {
        s_snap = heap_caps_calloc(1, sizeof(*s_snap), MALLOC_CAP_SPIRAM);
    }
    s_body = ui_box(parent);
    lv_obj_set_size(s_body, HUB_VIEW_W, HUB_VIEW_H);
    ui_flex(s_body, LV_FLEX_FLOW_ROW, UI_GAP);
    build_hero(s_body);
    lv_obj_t *right = ui_box(s_body);
    lv_obj_set_height(right, HUB_VIEW_H);
    lv_obj_set_flex_grow(right, 1);
    ui_flex(right, LV_FLEX_FLOW_COLUMN, UI_GAP);
    build_hourly(right);
    build_daily(right);
    build_status(parent);
    build_results(parent);
    weather_ui_update(NULL);
    return s_body;
}

static void update_view(void)
{
    const weather_state_t *s = s_snap;
    const wx_data_t *d = &s->data;
    ui_status_t state = status_of(s);
    switch (state) {
    case UI_STATUS_LOADING:
        ui_status_set(s_status, state, s->place.name, s->place.region);
        break;
    case UI_STATUS_ERROR:
        ui_status_set(s_status, state, "Couldn't load the forecast", s->place.name);
        break;
    default:
        ui_status_set(s_status, state, NULL, NULL);
        break;
    }
    lv_obj_set_hidden(s_body, state != UI_STATUS_NONE);
    if (state != UI_STATUS_NONE) {
        ui_spinner_run(s_refreshing, false);
        return;
    }
    lv_label_set_text(s_place, s->place.name);
    lv_obj_set_style_bg_color(s_unit_c, s->imperial ? COL_SURFACE2 : COL_SURFACE3, 0);
    lv_obj_set_style_bg_color(s_unit_f, s->imperial ? COL_SURFACE3 : COL_SURFACE2, 0);

    char t[48];
    bool stale = s->failed || (hub_time_valid() && s->fetched && time(NULL) - s->fetched > 2 * 3600);
    hhmm(t, sizeof(t), s->fetched ? s->fetched : d->now.time);
    lv_label_set_text_fmt(s_updated, "Updated %s", t);
    lv_obj_set_style_text_color(s_updated, stale ? COL_ACCENT_TEXT : COL_TEXT2, 0);
    ui_spinner_run(s_refreshing, s->fetching);

    set_look(s_now_icon, d->now.code, d->now.day);
    set_temp(s_now_temp, d->now.temp);
    lv_label_set_text(s_now_text, wx_code_text(d->now.code));
    lv_label_set_text_fmt(s_now_sub, "Feels %ld\xC2\xB0  \xC2\xB7  H %ld\xC2\xB0  L %ld\xC2\xB0", temp(d->now.feels),
                          temp(d->days[0].tmax), temp(d->days[0].tmin));

    lv_label_set_text_fmt(s_tile_value[0], "%ld %s", lroundf(s->imperial ? wx_kmh_to_mph(d->now.wind) : d->now.wind),
                          s->imperial ? "mph" : "km/h");
    if (d->now.humidity >= 0) {
        lv_label_set_text_fmt(s_tile_value[1], "%d%%", d->now.humidity);
    }
    if (d->days[0].uv >= 0) {
        lv_label_set_text_fmt(s_tile_value[2], "%ld %s", lroundf(d->days[0].uv), uv_word(d->days[0].uv));
    }
    if (d->days[0].sunrise && d->days[0].sunset) {
        char rise[8], set[8];
        hhmm(rise, sizeof(rise), d->days[0].sunrise);
        hhmm(set, sizeof(set), d->days[0].sunset);
        lv_label_set_text_fmt(s_tile_value[3], "%s / %s", rise, set);
    }

    for (int i = 0; i < WX_HOURS; i++) {
        bool on = i < d->n_hours;
        lv_obj_set_hidden(s_hour_col[i], !on);
        if (!on) {
            continue;
        }
        const wx_hour_t *h = &d->hours[i];
        struct tm tm;
        local_tm(h->time, &tm);
        if (i == 0) {
            lv_label_set_text(s_hour_time[i], "Now");
        } else {
            lv_label_set_text_fmt(s_hour_time[i], "%02d", tm.tm_hour);
        }
        set_look(s_hour_icon[i], h->code, h->day);
        set_temp(s_hour_temp[i], h->temp);
        if (h->pop >= 0) {
            lv_label_set_text_fmt(s_hour_pop[i], "%d%%", h->pop);
        } else {
            lv_label_set_text(s_hour_pop[i], "");
        }
        lv_obj_set_style_text_color(s_hour_pop[i], h->pop >= 30 ? COL_RAIN : COL_TEXT3, 0);
    }

    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < d->n_days; i++) {
        lo = fminf(lo, d->days[i].tmin);
        hi = fmaxf(hi, d->days[i].tmax);
    }
    float span = hi - lo > 1 ? hi - lo : 1;
    static const char *const wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (int i = 0; i < WX_DAYS; i++) {
        bool on = i < d->n_days;
        lv_obj_set_hidden(s_day_row[i], !on);
        if (!on) {
            continue;
        }
        const wx_day_t *day = &d->days[i];
        struct tm tm;
        local_tm(day->time + 12 * 3600, &tm); /* midday: safe from offset rounding */
        lv_label_set_text(s_day_name[i], i == 0 ? "Today" : wd[tm.tm_wday]);
        set_look(s_day_icon[i], day->code, true);
        if (day->pop >= 0) {
            lv_label_set_text_fmt(s_day_pop[i], "%d%%", day->pop);
        } else {
            lv_label_set_text(s_day_pop[i], "");
        }
        lv_obj_set_style_text_color(s_day_pop[i], day->pop >= 30 ? COL_RAIN : COL_TEXT3, 0);
        set_temp(s_day_lo[i], day->tmin);
        set_temp(s_day_hi[i], day->tmax);
        int left = (int)lroundf((day->tmin - lo) / span * 100);
        int width = (int)lroundf((day->tmax - day->tmin) / span * 100);
        lv_obj_set_x(s_day_fill[i], lv_pct(left));
        lv_obj_set_width(s_day_fill[i], lv_pct(width < 4 ? 4 : width));
        lv_obj_set_hidden(s_day_dot[i], i != 0);
        if (i == 0) {
            float now = fminf(fmaxf(d->now.temp, lo), hi);
            lv_obj_set_x(s_day_dot[i], lv_pct((int)lroundf((now - lo) / span * 100)));
        }
    }
}

/* --- Home card --------------------------------------------------------------- */

/* The hours panel gets what the current conditions leave of the card, which
 * changes with the temperature's width and the screen's size: show as many
 * hours as fit. */
static void on_hours_size(lv_event_t *e)
{
    lv_obj_t *hours = lv_event_get_current_target(e);
    int32_t fit = lv_obj_get_content_width(hours) / CARD_HOUR_W;
    for (int i = 0; i < CARD_HOURS; i++) {
        lv_obj_set_hidden(s_c_hour[i], i >= fit);
    }
    lv_obj_set_style_bg_opa(hours, fit > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

lv_obj_t *weather_ui_create_card(lv_obj_t *parent)
{
    if (!s_snap) {
        s_snap = heap_caps_calloc(1, sizeof(*s_snap), MALLOC_CAP_SPIRAM);
    }
    lv_obj_t *card = ui_panel(parent, UI_S(480), UI_S(260));
    lv_obj_set_style_pad_all(card, UI_S(22), 0);

    s_card_status = ui_status_create(card, LV_PCT(100), LV_PCT(100), true);
    ui_status_set_icon(s_card_status, UI_STATUS_EMPTY, &font_weather_96, ICON_W_CLOUD_SUN, COL_SUN_SOFT);

    s_card_body = ui_box(card);
    lv_obj_set_size(s_card_body, LV_PCT(100), LV_PCT(100));
    ui_flex(s_card_body, LV_FLEX_FLOW_ROW, UI_S(24));
    lv_obj_t *left = ui_box(s_card_body);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_PCT(100));
    ui_flex(left, LV_FLEX_FLOW_COLUMN, UI_S(6));
    lv_obj_t *head = ui_box(left);
    lv_obj_set_size(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_c_place = ui_label(head, &font_sora_16sb, COL_TEXT, "");
    s_c_spin = ui_spinner(head, UI_S(14), COL_TEXT2);
    ui_spinner_run(s_c_spin, false);
    lv_obj_t *now = ui_box(left);
    lv_obj_set_size(now, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(now, LV_FLEX_FLOW_ROW, UI_S(10));
    lv_obj_set_flex_align(now, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_c_icon = ui_label(now, &font_weather_96, COL_SUN, ICON_W_CLOUD_SUN);
    s_c_temp = ui_label(now, &font_sora_88n, COL_TEXT, "--");
    s_c_text = ui_label(left, &font_sora_15m, COL_TEXT, "");
    s_c_hl = ui_label(left, &font_sora_13m, COL_TEXT2, "");

    lv_obj_t *hours = ui_box(s_card_body);
    lv_obj_set_height(hours, LV_PCT(100));
    lv_obj_set_flex_grow(hours, 1);
    lv_obj_set_style_bg_color(hours, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(hours, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hours, ui_radius(14), 0);
    lv_obj_set_style_pad_all(hours, UI_S(12), 0);
    ui_flex(hours, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(hours, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(hours, on_hours_size, LV_EVENT_SIZE_CHANGED, NULL);
    for (int i = 0; i < CARD_HOURS; i++) {
        lv_obj_t *c = ui_box(hours);
        s_c_hour[i] = c;
        lv_obj_set_size(c, CARD_HOUR_W, LV_PCT(100));
        ui_flex(c, LV_FLEX_FLOW_COLUMN, 0);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_c_time[i] = ui_label(c, &font_mono_12, COL_TEXT2, "");
        s_c_hicon[i] = ui_label(c, &font_icons_28, COL_TEXT2, "");
        s_c_htemp[i] = ui_label(c, &font_sora_16sb, COL_TEXT, "");
    }
    weather_ui_update(NULL);
    return card;
}

static void update_card(void)
{
    const weather_state_t *s = s_snap;
    const wx_data_t *d = &s->data;
    ui_status_t state = status_of(s);
    switch (state) {
    case UI_STATUS_EMPTY:
        ui_status_set(s_card_status, state, "Weather", "Set location");
        break;
    case UI_STATUS_LOADING:
        ui_status_set(s_card_status, state, s->place.name, NULL);
        break;
    default:
        ui_status_set(s_card_status, state, s->place.name, "Couldn't load");
        break;
    }
    lv_obj_set_hidden(s_card_body, state != UI_STATUS_NONE);
    if (state != UI_STATUS_NONE) {
        ui_spinner_run(s_c_spin, false);
        return;
    }
    lv_label_set_text(s_c_place, s->place.name);
    ui_spinner_run(s_c_spin, s->fetching);
    set_look(s_c_icon, d->now.code, d->now.day);
    set_temp(s_c_temp, d->now.temp);
    lv_label_set_text(s_c_text, wx_code_text(d->now.code));
    lv_label_set_text_fmt(s_c_hl, "H %ld\xC2\xB0  L %ld\xC2\xB0", temp(d->days[0].tmax), temp(d->days[0].tmin));
    for (int i = 0; i < CARD_HOURS; i++) {
        bool on = i < d->n_hours;
        if (!on) {
            continue;
        }
        const wx_hour_t *h = &d->hours[i];
        struct tm tm;
        local_tm(h->time, &tm);
        if (i == 0) {
            lv_label_set_text(s_c_time[i], "Now");
        } else {
            lv_label_set_text_fmt(s_c_time[i], "%02d", tm.tm_hour);
        }
        set_look(s_c_hicon[i], h->code, h->day);
        set_temp(s_c_htemp[i], h->temp);
    }
}

/* --- Glance and updates ------------------------------------------------------ */

bool weather_ui_glance(hub_glance_t *out)
{
    weather_state_t *s = snap();
    if (!s) {
        return false;
    }
    weather_snapshot(s);
    if (!s->has_place || !s->data.valid) {
        return false;
    }
    wx_look_t look = wx_code_look(s->data.now.code, s->data.now.day);
    out->icon = look_icon(look);
    out->color = look_color(look);
    snprintf(out->text, sizeof(out->text), "%ld\xC2\xB0", temp(s->data.now.temp));
    return true;
}

void weather_ui_update(void *arg)
{
    if (!snap()) {
        return;
    }
    weather_snapshot(s_snap);
    if (s_body) {
        update_view();
    }
    if (s_card_body) {
        update_card();
    }
}
