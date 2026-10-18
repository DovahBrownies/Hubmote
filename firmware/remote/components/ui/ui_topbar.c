/*
 * Top bar: the remote tab (the remote in use; tapping it again opens the
 * switcher) and the Home tab (modules), the current context, a module
 * glance (e.g. the weather), Wi-Fi, battery and Settings.
 */
#include "hub_module.h"
#include "ui_internal.h"

static lv_obj_t *s_tab_remote;
static lv_obj_t *s_tab_home;
static lv_obj_t *s_remote_icon;
static lv_obj_t *s_remote_name;
static lv_obj_t *s_home_icon;
static lv_obj_t *s_home_label;
static ui_tab_t s_tab = UI_TAB_REMOTE;
static lv_obj_t *s_context;
static lv_obj_t *s_glance;
static lv_obj_t *s_glance_icon;
static lv_obj_t *s_glance_text;
static const hub_module_t *s_glance_module;
static lv_obj_t *s_wifi;
static lv_obj_t *s_battery_icon;
static lv_obj_t *s_battery_pct;

static void on_remote_tab(lv_event_t *e)
{
    if (ui_switcher_is_open()) {
        ui_switcher_close();
    } else if (s_tab == UI_TAB_REMOTE) {
        ui_switcher_open();
    } else {
        ui_remote_return();
    }
}

static void on_home_tab(lv_event_t *e)
{
    ui_switcher_close();
    ui_show_view(VIEW_DASH);
}

static void on_glance(lv_event_t *e)
{
    ui_switcher_close();
    if (s_glance_module) {
        ui_module_open(s_glance_module->id);
    }
}

static lv_obj_t *s_update_badge;

static void on_gear(lv_event_t *e)
{
    ui_switcher_close();
    if (ui_current_view() == VIEW_SETTINGS) {
        ui_remote_return();
    } else {
        ui_show_view(VIEW_SETTINGS);
    }
}

static lv_obj_t *tab(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *t = ui_btn(parent, LV_SIZE_CONTENT, UI_S(40), 10, COL_SURFACE1);
    lv_obj_set_style_pad_hor(t, UI_S(12), 0);
    ui_flex(t, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(t, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, NULL);
    return t;
}

static void style_tabs(void)
{
    bool remote = s_tab == UI_TAB_REMOTE, home = s_tab == UI_TAB_HOME;
    lv_obj_set_style_bg_color(s_tab_remote, remote ? COL_SURFACE3 : COL_SURFACE1, 0);
    lv_obj_set_style_bg_color(s_tab_home, home ? COL_SURFACE3 : COL_SURFACE1, 0);
    lv_obj_set_style_text_color(s_remote_name, remote ? COL_TEXT : COL_TEXT2, 0);
    lv_obj_set_style_text_color(s_home_label, home ? COL_TEXT : COL_TEXT2, 0);
    lv_obj_set_style_text_color(s_home_icon, home ? COL_ACCENT_TEXT : COL_TEXT2, 0);
}

lv_obj_t *ui_topbar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = ui_box(parent);
    lv_obj_set_size(bar, UI_W, UI_TOPBAR_H);
    lv_obj_set_style_pad_hor(bar, UI_GAP, 0);
    ui_flex(bar, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Tabs */
    lv_obj_t *tabs = ui_box(bar);
    lv_obj_set_size(tabs, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(tabs, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tabs, UI_S(1), 0);
    lv_obj_set_style_border_color(tabs, COL_LINE, 0);
    lv_obj_set_style_radius(tabs, ui_radius(14), 0);
    lv_obj_set_style_pad_all(tabs, UI_S(3), 0);
    ui_flex(tabs, LV_FLEX_FLOW_ROW, UI_S(4));

    s_tab_remote = tab(tabs, on_remote_tab);
    s_remote_icon = ui_label(s_tab_remote, &font_icons_20, COL_ACCENT_TEXT, ICON_STREAMER);
    s_remote_name = ui_label(s_tab_remote, &font_sora_13m, COL_TEXT, "Google TV Streamer");
    lv_label_set_long_mode(s_remote_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(s_remote_name, UI_S(190), 0);
    ui_label(s_tab_remote, &font_icons_16, COL_TEXT2, ICON_CHEVRON_DOWN);

    s_tab_home = tab(tabs, on_home_tab);
    s_home_icon = ui_label(s_tab_home, &font_icons_20, COL_TEXT2, ICON_GRID);
    s_home_label = ui_label(s_tab_home, &font_sora_13m, COL_TEXT2, "Home");
    style_tabs();

    /* Context pill, centered in the remaining space */
    lv_obj_t *mid = ui_box(bar);
    lv_obj_set_height(mid, UI_TOPBAR_H);
    lv_obj_set_flex_grow(mid, 1);
    ui_flex(mid, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pill = ui_box(mid);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, UI_S(32));
    lv_obj_set_style_pad_hor(pill, UI_S(14), 0);
    lv_obj_set_style_radius(pill, ui_radius(LV_RADIUS_CIRCLE), 0);
    lv_obj_set_style_bg_color(pill, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    ui_flex(pill, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_context = ui_label(pill, &font_sora_13m, COL_TEXT, "Setting up");

    /* Module glance (hidden until a module has one) */
    s_glance = ui_btn(bar, LV_SIZE_CONTENT, UI_S(36), LV_RADIUS_CIRCLE, COL_SURFACE1);
    lv_obj_set_style_pad_hor(s_glance, UI_S(12), 0);
    lv_obj_set_style_border_width(s_glance, UI_S(1), 0);
    lv_obj_set_style_border_color(s_glance, COL_LINE, 0);
    ui_flex(s_glance, LV_FLEX_FLOW_ROW, UI_S(6));
    lv_obj_set_flex_align(s_glance, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(s_glance, on_glance, LV_EVENT_CLICKED, NULL);
    s_glance_icon = ui_label(s_glance, &font_icons_20, COL_ACCENT_TEXT, "");
    s_glance_text = ui_label(s_glance, &font_sora_15m, COL_TEXT, "");
    lv_obj_set_hidden(s_glance, true);

    /* Status */
    lv_obj_t *status = ui_box(bar);
    lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(status, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_wifi = ui_label(status, &font_icons_20, COL_TEXT3, ICON_WIFI_OFF);
    lv_obj_set_style_pad_right(s_wifi, UI_S(6), 0);
    s_battery_icon = ui_label(status, &font_icons_20, COL_TEXT2, ICON_BATTERY);
    s_battery_pct = ui_label(status, &font_mono_12, COL_TEXT2, "--%");

    lv_obj_t *gear = ui_btn_icon(bar, UI_S(40), UI_S(40), 10, COL_SURFACE1, &font_icons_20, ICON_GEAR, COL_TEXT2, NULL, false);
    lv_obj_set_style_border_width(gear, UI_S(1), 0);
    lv_obj_set_style_border_color(gear, COL_LINE, 0);
    lv_obj_add_event_cb(gear, on_gear, LV_EVENT_CLICKED, NULL);
    s_update_badge = ui_badge(gear, -UI_S(5), UI_S(5));
    return bar;
}

void ui_topbar_set_update_badge(bool on)
{
    lv_obj_set_hidden(s_update_badge, !on);
}

void ui_topbar_set_tab(ui_tab_t t)
{
    if (t != s_tab) {
        s_tab = t;
        style_tabs();
    }
}

void ui_topbar_refresh_glance(void)
{
    hub_glance_t g = {0};
    s_glance_module = NULL;
    for (size_t i = 0; i < ui_home_module_count() && !s_glance_module; i++) { /* the first in the Home tab's order */
        const hub_module_t *m = ui_home_module(i);
        if (hub_module_enabled(m) && m->glance && m->glance(&g)) {
            s_glance_module = m;
        }
    }
    lv_obj_set_hidden(s_glance, !s_glance_module);
    if (s_glance_module) {
        lv_label_set_text(s_glance_icon, g.icon ? g.icon : "");
        lv_obj_set_style_text_color(s_glance_icon, g.color, 0);
        lv_label_set_text(s_glance_text, g.text);
    }
}

void ui_topbar_set_context(const char *context)
{
    lv_label_set_text(s_context, context);
}

void ui_topbar_set_target(const char *target)
{
    lv_label_set_text(s_remote_name, target);
}

void ui_topbar_set_ir(bool ir)
{
    lv_label_set_text(s_remote_icon, ir ? ICON_IR : ICON_STREAMER);
    lv_obj_set_style_text_color(s_remote_icon, ir ? COL_IR : COL_ACCENT_TEXT, 0);
}

void ui_topbar_set_battery(int percent, bool charging)
{
    bool low = !charging && percent >= 0 && percent < UI_BATTERY_RED;
    const char *icon = charging ? ICON_BATTERY_CHARGING : (low ? ICON_BATTERY_LOW : ICON_BATTERY);
    lv_color_t color = charging ? COL_GOOD : (low ? COL_BAD : COL_TEXT2);
    lv_label_set_text(s_battery_icon, icon);
    lv_obj_set_style_text_color(s_battery_icon, color, 0);
    lv_obj_set_style_text_color(s_battery_pct, color, 0);
    if (percent < 0) {
        lv_label_set_text(s_battery_pct, "--%");
    } else {
        lv_label_set_text_fmt(s_battery_pct, "%d%%", percent);
    }
}

void ui_topbar_show_battery(bool shown)
{
    lv_obj_set_hidden(s_battery_icon, !shown);
    lv_obj_set_hidden(s_battery_pct, !shown);
}

void ui_topbar_set_wifi(bool up)
{
    lv_label_set_text(s_wifi, up ? ICON_WIFI : ICON_WIFI_OFF);
    lv_obj_set_style_text_color(s_wifi, up ? COL_TEXT2 : COL_ACCENT_TEXT, 0);
}
