/*
 * Screen assembly, view switching and the thread-safe public API.
 *
 * Layout: top bar across the top; below it the rail, then the "stage". The
 * stage holds either a 440 px context panel with the touchpad beside it
 * (Google TV screens), or one wide view. Screens that aren't the Google
 * TV's drop the rail and use the full width. Every view is built once and
 * shown or hidden.
 *
 * Navigation: the remote tab shows the remote in use (tap again for the
 * switcher), the Home tab shows the modules, the gear opens Settings.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "ui_internal.h"

static const char *TAG = "ui";

static ui_action_cb_t s_cb;
static void *s_cb_ctx;
static bool s_ready;
static bool s_main;
static ui_view_t s_view = VIEW_SETUP;
static lv_obj_t *s_views[VIEW_COUNT];
static lv_obj_t *s_pad;
static lv_obj_t *s_rail;
static lv_obj_t *s_stage;
static lv_obj_t *s_sleep_layer;
static lv_obj_t *s_battery_card; /* low-battery scrim and card */
static bool s_battery_warned;    /* shown in this discharge; cleared on USB */
static ui_view_t s_remote_view = VIEW_TV_HOME; /* the remote the remote tab returns to */
static bool s_tv_on = true; /* the Streamer is awake (remote_start); asleep it still reports its last app */
static char s_remote_name[32];
static lv_obj_t *s_toast;
static lv_obj_t *s_toast_icon;
static lv_obj_t *s_toast_label;
static lv_timer_t *s_toast_timer;
static char s_app[128];
static int s_last_volume = -1;
static ui_link_t s_link = UI_LINK_OFFLINE;
static char s_tv_name[64] = "Google TV Streamer";
/* The text field the TV last reported focused (remote_ime_show_request).
 * The protocol never says when it loses focus. The companion app does
 * (s_field_kind); without it, the field counts as gone once the app changes
 * or a key that may move the focus is sent. */
static bool s_field_active;
static ui_field_t s_field_kind = UI_FIELD_UNKNOWN;
static int s_field_id;
static char s_field_value[128];
static char s_field_label[64];

#define LOCK()                                 \
    do {                                       \
        if (!s_ready || !lvgl_port_lock(0)) { \
            return;                            \
        }                                      \
    } while (0)
#define UNLOCK() lvgl_port_unlock()

/* --- Dispatch ------------------------------------------------------------- */

void ui_emit(const ui_action_t *action)
{
    if (!s_cb) {
        return;
    }
    /* Keys and IR buttons go to the device whose screen they came from: on
     * the IR TV's screen its keys and the touchpad's arrows go to the TV.
     * Volume goes through the Streamer (HDMI-CEC) everywhere, unless
     * Settings sends it to the TV's IR. */
    ui_action_t a = *action;
    if (a.type == UI_ACT_KEY) {
        bool volume = a.key == UI_KEY_VOLUME_UP || a.key == UI_KEY_VOLUME_DOWN || a.key == UI_KEY_VOLUME_MUTE;
        a.target = volume ? (ui_irtv_volume() ? UI_TARGET_IR_TV : UI_TARGET_STREAMER)
                   : s_view == VIEW_IR_TV ? UI_TARGET_IR_TV
                                          : UI_TARGET_STREAMER;
        if (a.target == UI_TARGET_STREAMER && !volume && s_field_kind == UI_FIELD_UNKNOWN) {
            s_field_active = false;
        }
    } else if (a.type == UI_ACT_IR) {
        a.target = s_view == VIEW_IR_TV ? UI_TARGET_IR_TV : UI_TARGET_LEARNED;
    }
    s_cb(&a, s_cb_ctx);
    if (a.type == UI_ACT_KEY && s_view == VIEW_KEYBOARD && (a.key == UI_KEY_BACK || a.key == UI_KEY_HOME)) {
        ui_kb_close(); /* the TV closes its keyboard on these too */
    }
}

void ui_emit_key(int key, ui_dir_t dir)
{
    ui_action_t a = {.type = UI_ACT_KEY, .key = key, .dir = dir};
    ui_emit(&a);
}

/* --- Views ---------------------------------------------------------------- */

/* What each view takes from the screen around it: one row per view, so a
 * new view can't be left out of a list. */
enum {
    VF_NO_PAD = 1 << 0,  /* no touchpad for the Streamer */
    VF_NO_RAIL = 1 << 1, /* no rail (the Streamer's keys) */
    VF_FULL = 1 << 2,    /* widens into the rail's space (without: a centered column) */
    VF_REMOTE = 1 << 3,  /* a remote's screen, which the remote tab returns to */
    VF_NOT_TV = 1 << 4,  /* the top bar shows another device or a module, not the Google TV */
};

static const uint8_t k_view_flags[VIEW_COUNT] = {
    [VIEW_SETUP] = VF_NO_PAD,
    [VIEW_TV_HOME] = VF_REMOTE,
    [VIEW_APP] = VF_REMOTE,
    [VIEW_KEYBOARD] = VF_NO_PAD,
    [VIEW_SETTINGS] = VF_NO_PAD | VF_NO_RAIL | VF_FULL,
    /* The IR TV keeps the touchpad: its arrows and OK are the TV's. */
    [VIEW_IR_TV] = VF_NO_RAIL | VF_FULL | VF_REMOTE | VF_NOT_TV,
    /* Another device's: a phone-width column. */
    [VIEW_IR_REMOTE] = VF_NO_PAD | VF_NO_RAIL | VF_REMOTE | VF_NOT_TV,
    [VIEW_INPUTS] = VF_NO_PAD | VF_NO_RAIL | VF_REMOTE | VF_NOT_TV,
    [VIEW_LEARN] = VF_NO_PAD | VF_NO_RAIL | VF_FULL | VF_NOT_TV,
    [VIEW_TV_SETUP] = VF_NO_PAD | VF_NO_RAIL | VF_FULL | VF_NOT_TV,
    [VIEW_DASH] = VF_NO_PAD | VF_NO_RAIL | VF_FULL | VF_NOT_TV,
    [VIEW_MODULE] = VF_NO_PAD | VF_NO_RAIL | VF_FULL | VF_NOT_TV,
};

static bool view_has(ui_view_t v, unsigned flag)
{
    return (unsigned)v < VIEW_COUNT && (k_view_flags[v] & flag);
}

void ui_show_view(ui_view_t view)
{
    /* The keyboard for the remote's own text (a name, a city) belongs to the
     * screen that opened it: no rail, same tab, and the TV isn't told. */
    bool local_kb = view == VIEW_KEYBOARD && ui_kb_is_local();
    if ((s_view == VIEW_KEYBOARD) != (view == VIEW_KEYBOARD) && !ui_kb_is_local()) {
        ui_action_t a = {.type = UI_ACT_KEYBOARD_SHOWN, .index = view == VIEW_KEYBOARD};
        ui_emit(&a);
    }
    if (s_view == VIEW_MODULE && view != VIEW_MODULE) {
        ui_modview_leave();
    }
    if (s_view == VIEW_IR_REMOTE && view != VIEW_IR_REMOTE && view != VIEW_KEYBOARD) {
        ui_ir_remote_leave(); /* ends editing (the keyboard renames while editing) */
    }
    if (view_has(view, VF_REMOTE)) {
        s_remote_view = view;
    }
    ui_switcher_close();
    ui_view_t tab_view = local_kb ? s_view : view;
    ui_topbar_set_tab(tab_view == VIEW_DASH || tab_view == VIEW_MODULE ? UI_TAB_HOME
                      : tab_view == VIEW_SETTINGS                       ? UI_TAB_NONE
                                                                       : UI_TAB_REMOTE);
    if (view == VIEW_DASH) {
        ui_dash_show();
    }
    if (view == VIEW_SETTINGS) {
        ui_topbar_set_context(ui_settings_version_text());
        ui_settings_shown();
    }
    for (int i = 0; i < VIEW_COUNT; i++) {
        lv_obj_set_hidden(s_views[i], i != (int)view);
    }
    bool no_rail = view_has(view, VF_NO_RAIL) || local_kb;
    int32_t stage_w = view_has(view, VF_FULL) ? UI_WIDE_W : UI_STAGE_W;
    lv_obj_set_hidden(s_pad, view_has(view, VF_NO_PAD));
    lv_obj_set_hidden(s_rail, no_rail);
    lv_obj_set_width(s_stage, stage_w);
    lv_obj_set_x(s_stage, no_rail ? (UI_W - stage_w) / 2 : UI_GAP + UI_RAIL_W + UI_GAP);
    lv_obj_set_x(s_pad, stage_w - UI_PAD_W);
    s_view = view;
    ui_rail_set_kb(view == VIEW_KEYBOARD);
}

ui_view_t ui_current_view(void)
{
    return s_view;
}

static void apply_context(void)
{
    if (!s_tv_on) {
        ui_topbar_set_context("TV off");
        return;
    }
    if (ui_apps_is_launcher(s_app)) {
        ui_topbar_set_context("Home");
    } else {
        const ui_app_info_t *app = ui_apps_find(s_app);
        ui_app_set(app, s_app);
        ui_topbar_set_context(app ? app->name : s_app);
    }
}

void ui_return_to_context(void)
{
    ui_topbar_set_target(s_tv_name);
    ui_topbar_set_ir(false);
    if (!s_main) {
        ui_topbar_set_context("Setting up");
        ui_show_view(VIEW_SETUP);
        return;
    }
    apply_context();
    /* Asleep, the Streamer still reports the last app, but wakes on Home. */
    ui_show_view(!s_tv_on || ui_apps_is_launcher(s_app) ? VIEW_TV_HOME : VIEW_APP);
}

/* How the keyboard reaches the TV right now: the TV's field, a field the
 * companion sees (a browser, an app's own keyboard), Google TV's search on
 * Home, or text and key presses. */
static ui_kb_mode_t kb_mode_now(void)
{
    if (s_field_active || s_field_kind == UI_FIELD_TEXT || s_field_kind == UI_FIELD_PASSWORD ||
        s_field_kind == UI_FIELD_NUMBER) {
        return UI_KB_FIELD;
    }
    return ui_apps_is_launcher(s_app) ? UI_KB_SEARCH : UI_KB_KEYS;
}

void ui_keyboard_toggle(void)
{
    if (!s_main) {
        return; /* nothing to type into during setup */
    }
    if (s_view == VIEW_KEYBOARD) {
        ui_kb_close();
        return;
    }
    ui_kb_mode_t mode = kb_mode_now();
    ui_topbar_set_context(mode == UI_KB_SEARCH ? "Search" : "Typing");
    /* With the companion's field (a browser, an app's own keyboard) the text
     * starts empty: the companion edits it from there. */
    ui_kb_open(mode, s_field_active ? s_field_value : NULL, s_field_active ? s_field_label : NULL);
}

void ui_remote_return(void)
{
    if (!s_main || s_remote_view == VIEW_TV_HOME || s_remote_view == VIEW_APP) {
        ui_return_to_context();
    } else if (s_remote_view == VIEW_INPUTS) {
        ui_inputs_show();
    } else {
        ui_show_ir_view(s_remote_view, s_remote_name);
    }
}

const char *ui_tv_display_name(void)
{
    return s_tv_name;
}

const char *ui_tv_meta(void)
{
    if (!s_main || s_link != UI_LINK_CONNECTED) {
        return s_link == UI_LINK_CONNECTING ? "Reconnecting" : "Offline";
    }
    if (!s_tv_on) {
        return "Off";
    }
    if (ui_apps_is_launcher(s_app)) {
        return "Home";
    }
    const ui_app_info_t *app = ui_apps_find(s_app);
    return app ? app->name : "";
}

void ui_show_ir_view(ui_view_t view, const char *device_name)
{
    if (device_name != s_remote_name) {
        strlcpy(s_remote_name, device_name, sizeof(s_remote_name));
    }
    ui_topbar_set_target(device_name);
    ui_topbar_set_ir(true);
    ui_topbar_set_context(device_name);
    ui_show_view(view);
}

/* --- Toast and sleep layer ------------------------------------------------ */

static void toast_hide(lv_timer_t *t)
{
    lv_obj_set_hidden(s_toast, true);
    lv_timer_pause(t);
}

static void toast(const char *icon, const char *text)
{
    lv_label_set_text(s_toast_icon, icon);
    lv_label_set_text(s_toast_label, text);
    lv_obj_set_hidden(s_toast, false);
    lv_timer_reset(s_toast_timer);
    lv_timer_resume(s_toast_timer);
}

void ui_toast(const char *icon, const char *text)
{
    toast(icon, text);
}

static void on_battery_ok(lv_event_t *e)
{
    lv_obj_set_hidden(s_battery_card, true);
}

/* Scrim over everything (a tap outside the card dismisses it too) and a
 * small card. Under the sleep layer, so the tap that wakes the screen
 * doesn't dismiss it unseen. */
static void build_battery_card(void)
{
    s_battery_card = ui_box(lv_layer_top());
    lv_obj_set_size(s_battery_card, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_battery_card, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_battery_card, LV_OPA_70, 0);
    lv_obj_set_clickable(s_battery_card, true);
    lv_obj_add_event_cb(s_battery_card, on_battery_ok, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = ui_panel(s_battery_card, UI_S(400), LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_clickable(card, true); /* taps on the card itself don't dismiss */
    lv_obj_set_style_border_width(card, UI_S(1), 0);
    lv_obj_set_style_border_color(card, COL_LINE2, 0);
    lv_obj_set_style_pad_all(card, UI_S(28), 0);
    ui_flex(card, LV_FLEX_FLOW_COLUMN, UI_S(10));
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(card, &font_icons_28, COL_BAD, ICON_BATTERY_LOW);
    ui_label(card, &font_sora_20sb, COL_TEXT, "Battery low");
    ui_label(card, &font_sora_15m, COL_TEXT2, "Please charge soon");
    lv_obj_t *ok = ui_btn_text(card, UI_S(160), UI_S(48), LV_RADIUS_CIRCLE, COL_SURFACE3, &font_sora_15m, COL_TEXT, "OK");
    lv_obj_set_style_margin_top(ok, UI_S(10), 0);
    lv_obj_add_event_cb(ok, on_battery_ok, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(s_battery_card, true);
}

static void on_sleep_layer(lv_event_t *e)
{
    /* The tap that wakes the screen does nothing else. */
    ESP_LOGI(TAG, "wake tap");
    lv_obj_set_hidden(s_sleep_layer, true);
}

static void glance_async(void *arg)
{
    ui_topbar_refresh_glance();
}

static void on_glance_changed(void)
{
    hub_ui_async(glance_async, NULL);
}

static void modules_async(void *arg)
{
    ui_dash_refresh();
    ui_settings_modules_changed();
    ui_topbar_refresh_glance();
}

/* Modules started, or one was switched on or off in Settings. */
static void on_modules_changed(void)
{
    hub_ui_async(modules_async, NULL);
}

static void build_overlays(void)
{
    ui_switcher_build();

    s_toast = ui_box(lv_layer_top());
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, UI_S(44));
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, UI_TOPBAR_H + UI_S(8));
    lv_obj_set_style_pad_hor(s_toast, UI_S(18), 0);
    lv_obj_set_style_radius(s_toast, ui_radius(LV_RADIUS_CIRCLE), 0);
    lv_obj_set_style_bg_color(s_toast, COL_SURFACE3, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_toast, UI_S(1), 0);
    lv_obj_set_style_border_color(s_toast, COL_LINE2, 0);
    ui_flex(s_toast, LV_FLEX_FLOW_ROW, UI_S(10));
    lv_obj_set_flex_align(s_toast, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_toast_icon = ui_label(s_toast, &font_icons_20, COL_ACCENT_TEXT, "");
    s_toast_label = ui_label(s_toast, &font_sora_15m, COL_TEXT, "");
    lv_obj_set_hidden(s_toast, true);
    s_toast_timer = lv_timer_create(toast_hide, 1400, NULL);
    lv_timer_pause(s_toast_timer);

    build_battery_card();

    s_sleep_layer = ui_box(lv_layer_top());
    lv_obj_set_size(s_sleep_layer, UI_W, UI_H);
    lv_obj_set_clickable(s_sleep_layer, true);
    lv_obj_add_event_cb(s_sleep_layer, on_sleep_layer, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(s_sleep_layer, true);
}

/* --- Build ---------------------------------------------------------------- */

void ui_init(ui_action_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    if (!lvgl_port_lock(0)) {
        return;
    }

    /* LVGL's own widget defaults (scrollbars, sliders...) follow the theme. */
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, COL_ACCENT, COL_IR, !ui_theme()->light, &font_sora_15m));

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *top = ui_topbar_create(scr);
    lv_obj_set_pos(top, 0, 0);

    lv_obj_t *rail = s_rail = ui_rail_create(scr);
    lv_obj_set_pos(rail, UI_GAP, UI_TOPBAR_H);

    lv_obj_t *stage = s_stage = ui_box(scr);
    lv_obj_set_size(stage, UI_STAGE_W, UI_BODY_H);
    lv_obj_set_pos(stage, UI_GAP + UI_RAIL_W + UI_GAP, UI_TOPBAR_H);

    s_views[VIEW_SETUP] = ui_setup_create(stage);
    s_views[VIEW_TV_HOME] = ui_tv_home_create(stage);
    s_views[VIEW_APP] = ui_app_create(stage);
    s_views[VIEW_KEYBOARD] = ui_kb_create(stage);
    s_views[VIEW_SETTINGS] = ui_settings_create(stage);
    s_views[VIEW_IR_TV] = ui_ir_tv_create(stage);
    s_views[VIEW_IR_REMOTE] = ui_ir_remote_create(stage);
    lv_obj_set_x(s_views[VIEW_IR_REMOTE], (UI_STAGE_W - UI_CENTER_W) / 2); /* shown alone, centered */
    s_views[VIEW_LEARN] = ui_learn_create(stage);
    s_views[VIEW_INPUTS] = ui_inputs_create(stage);
    lv_obj_set_x(s_views[VIEW_INPUTS], (UI_STAGE_W - UI_CENTER_W) / 2);
    s_views[VIEW_DASH] = ui_dash_create(stage);
    s_views[VIEW_MODULE] = ui_modview_create(stage);
    s_views[VIEW_TV_SETUP] = ui_tv_setup_create(stage);
    s_pad = ui_pad_create(stage);
    lv_obj_set_pos(s_pad, UI_CENTER_W + UI_GAP, 0);

    build_overlays();
    hub_set_glance_listener(on_glance_changed);
    hub_set_modules_listener(on_modules_changed);
    s_ready = true;
    ui_topbar_set_context("Setting up");
    ui_show_view(VIEW_SETUP);
    lvgl_port_unlock();
}

/* --- Live state ----------------------------------------------------------- */

/* True while the top bar describes the Google TV (not an IR device). */
static bool tv_on_top(void)
{
    return s_main && !view_has(s_view, VF_NOT_TV);
}

/* True while the context chip follows the Google TV (Settings shows the
 * firmware version there instead). */
static bool tv_context(void)
{
    return tv_on_top() && s_view != VIEW_SETTINGS;
}

void ui_set_link(ui_link_t link)
{
    LOCK();
    s_link = link;
    ui_settings_set_tv(NULL, link);
    if (tv_context() && link != UI_LINK_CONNECTED) {
        ui_topbar_set_context(link == UI_LINK_CONNECTING ? "Reconnecting" : "Offline");
    } else if (tv_context()) {
        apply_context();
    }
    UNLOCK();
}

void ui_set_tv_name(const char *name)
{
    LOCK();
    strlcpy(s_tv_name, name ? name : "", sizeof(s_tv_name));
    ui_settings_set_tv(name, s_link);
    if (tv_on_top() || !s_main) {
        ui_topbar_set_target(name);
    }
    UNLOCK();
}

void ui_set_tv_power(bool on)
{
    LOCK();
    s_tv_on = on;
    if (s_main && (s_view == VIEW_TV_HOME || s_view == VIEW_APP)) {
        ui_return_to_context();
    } else if (tv_context()) {
        apply_context(); /* another screen is up: the context shows again on return */
    }
    UNLOCK();
}

void ui_set_app(const char *package)
{
    LOCK();
    if (strcmp(s_app, package ? package : "") != 0) {
        s_field_active = false;
        ui_kb_clear();
    }
    strlcpy(s_app, package ? package : "", sizeof(s_app));
    if (s_view == VIEW_KEYBOARD) {
        ui_kb_follow(kb_mode_now()); /* e.g. from Home's search into an app */
    }
    if (s_main && (s_view == VIEW_TV_HOME || s_view == VIEW_APP)) {
        ui_return_to_context();
    } else if (tv_context()) {
        apply_context();
    }
    UNLOCK();
}

void ui_set_volume(int level, int max, bool muted)
{
    LOCK();
    ui_rail_set_volume(level, max, muted);
    ui_ir_tv_set_muted(muted);
    if (s_last_volume >= 0 && level != s_last_volume) {
        char text[24];
        lv_snprintf(text, sizeof(text), "Volume %d", level);
        toast(ICON_VOLUME, text);
    }
    s_last_volume = level;
    UNLOCK();
}

void ui_ime_show(const char *value, const char *label)
{
    LOCK();
    s_field_active = true;
    strlcpy(s_field_value, value ? value : "", sizeof(s_field_value));
    strlcpy(s_field_label, label ? label : "", sizeof(s_field_label));
    if (s_main && tv_on_top()) { /* not over another device's screen or a module */
        ui_topbar_set_context("Typing");
        ui_kb_open(UI_KB_FIELD, s_field_value, s_field_label);
    }
    UNLOCK();
}

void ui_ime_text(const char *value)
{
    LOCK();
    strlcpy(s_field_value, value ? value : "", sizeof(s_field_value));
    /* While typing through the companion, the remote's text is the truth:
     * the TV echoes it back late, and adopting that mid-edit makes the text
     * jump (seen while holding Backspace). */
    bool companion = s_field_kind == UI_FIELD_TEXT || s_field_kind == UI_FIELD_PASSWORD ||
                     s_field_kind == UI_FIELD_NUMBER;
    if (s_view == VIEW_KEYBOARD && !companion) {
        ui_kb_set_text(value);
    }
    UNLOCK();
}

void ui_set_field(ui_field_t field, int id)
{
    LOCK();
    if (field != s_field_kind || id != s_field_id) {
        ui_kb_clear();
    }
    s_field_kind = field;
    s_field_id = id;
    if (field == UI_FIELD_NONE) {
        s_field_active = false;
    }
    ui_kb_set_field(field);
    if (s_view == VIEW_KEYBOARD) {
        ui_kb_follow(kb_mode_now());
    }
    UNLOCK();
}

void ui_set_apps(const ui_app_t *apps, size_t count)
{
    LOCK();
    ui_apps_set_tv(apps, count);
    ui_tv_home_refresh();
    /* The app panel may show an icon from the old list, and the context
     * the package of an app the list now names. */
    if (s_app[0] && !ui_apps_is_launcher(s_app)) {
        ui_app_set(ui_apps_find(s_app), s_app);
        if (tv_context()) {
            apply_context();
        }
    }
    UNLOCK();
}

void ui_set_app_layout(const char *order, const char *hidden)
{
    LOCK();
    ui_apps_load_layout(order, hidden);
    ui_tv_home_refresh();
    UNLOCK();
}

void ui_app_layout(char *order, char *hidden)
{
    order[0] = '\0';
    hidden[0] = '\0';
    LOCK();
    ui_apps_copy_layout(order, hidden);
    UNLOCK();
}

void ui_set_battery(int percent, bool charging)
{
    LOCK();
    ui_topbar_set_battery(percent, charging);
    if (charging) {
        s_battery_warned = false;
        lv_obj_set_hidden(s_battery_card, true);
    } else if (percent >= 0 && percent < UI_BATTERY_WARN && !s_battery_warned) {
        ESP_LOGI(TAG, "battery low (%d%%)", percent);
        s_battery_warned = true;
        lv_obj_set_hidden(s_battery_card, false);
    }
    UNLOCK();
}

static bool s_wifi_up;

void ui_set_wifi(bool up)
{
    LOCK();
    s_wifi_up = up;
    ui_topbar_set_wifi(up);
    ui_settings_wifi_changed();
    UNLOCK();
}

bool ui_wifi_is_up(void)
{
    return s_wifi_up;
}

void ui_set_wifi_networks(const ui_wifi_ap_t *aps, size_t count)
{
    LOCK();
    ui_wifi_show_networks(aps, count);
    UNLOCK();
}

void ui_set_wifi_network(const char *ssid)
{
    LOCK();
    ui_wifi_set_current(ssid);
    ui_settings_wifi_changed();
    UNLOCK();
}

void ui_set_brightness(int percent)
{
    LOCK();
    ui_settings_set_brightness(percent);
    UNLOCK();
}

void ui_set_media(const char *pkg, const char *title, const char *subtitle, bool playing)
{
    LOCK();
    ui_app_set_media(pkg, title, subtitle, playing);
    UNLOCK();
}

void ui_set_screen_turned(bool turned)
{
    LOCK();
    ui_settings_set_turned(turned);
    UNLOCK();
}

void ui_set_screen_timeouts(const ui_timeouts_t *t)
{
    LOCK();
    ui_settings_set_timeouts(t);
    UNLOCK();
}

void ui_set_hostname(const char *name)
{
    LOCK();
    ui_settings_set_hostname(name);
    UNLOCK();
}

void ui_set_time_zone(const char *name)
{
    LOCK();
    ui_settings_set_time_zone(name);
    UNLOCK();
}

/* An update waits: firmware (Settings > About) or the companion (Settings >
 * Remotes). Dots on the gear and those sections say so. */
static bool s_update_waits;
static bool s_companion_update_waits;

static void show_update_badges(void)
{
    ui_topbar_set_update_badge(s_update_waits || s_companion_update_waits);
    ui_settings_set_update_badges(s_update_waits, s_companion_update_waits);
}

void ui_set_update(ui_update_t state, const char *version, int percent)
{
    LOCK();
    ui_settings_set_update(state, version, percent);
    s_update_waits = state == UI_UPDATE_AVAILABLE;
    show_update_badges();
    UNLOCK();
}

void ui_set_update_token(const char *token)
{
    LOCK();
    ui_settings_set_token(token);
    UNLOCK();
}

void ui_set_about(const ui_about_t *about)
{
    LOCK();
    ui_settings_set_about(about);
    UNLOCK();
}

void ui_set_build(const char *version, const char *date, const char *time)
{
    LOCK();
    ui_settings_set_build(version, date, time);
    UNLOCK();
}

void ui_set_asleep(bool asleep)
{
    LOCK();
    lv_obj_set_hidden(s_sleep_layer, !asleep);
    UNLOCK();
}

uint32_t ui_idle_ms(void)
{
    if (!s_ready || !lvgl_port_lock(0)) {
        return 0;
    }
    uint32_t ms = lv_display_get_inactive_time(NULL);
    lvgl_port_unlock();
    return ms;
}

/* --- Setup ---------------------------------------------------------------- */

static void enter_setup(void)
{
    s_main = false;
    ui_topbar_set_context("Setting up");
    ui_show_view(VIEW_SETUP);
}

void ui_setup_busy(const char *title, const char *body)
{
    LOCK();
    ui_setup_show_busy(title, body);
    enter_setup();
    UNLOCK();
}

void ui_setup_wifi(const char *message)
{
    LOCK();
    enter_setup();
    ui_setup_show_wifi(message);
    UNLOCK();
}

void ui_setup_tvs(const ui_tv_t *tvs, size_t count)
{
    LOCK();
    ui_setup_show_tvs(tvs, count);
    enter_setup();
    UNLOCK();
}

void ui_setup_code(const char *tv_name)
{
    LOCK();
    ui_setup_show_code(tv_name);
    enter_setup();
    UNLOCK();
}

void ui_setup_code_wrong(void)
{
    LOCK();
    ui_setup_show_code_wrong();
    UNLOCK();
}

void ui_setup_error(const char *title, const char *body)
{
    LOCK();
    ui_setup_show_error(title, body);
    enter_setup();
    UNLOCK();
}

void ui_show_main(void)
{
    LOCK();
    s_main = true;
    ui_return_to_context();
    UNLOCK();
}

/* --- The companion app ------------------------------------------------------ */

void ui_set_companion(ui_companion_t state)
{
    LOCK();
    ui_settings_set_companion(state);
    UNLOCK();
}

void ui_set_companion_version(const char *version, ui_companion_update_t update, const char *next, bool can_install)
{
    LOCK();
    ui_settings_set_companion_version(version, update, next, can_install);
    s_companion_update_waits = update == UI_COMPANION_UPDATE_AVAILABLE && can_install;
    show_update_badges();
    UNLOCK();
}

void ui_companion_pairing(bool code_shown, uint32_t locked_s)
{
    LOCK();
    if (code_shown) {
        ui_setup_show_companion_code();
        ui_topbar_set_context("Companion app");
        ui_show_view(VIEW_SETUP);
    } else if (locked_s > 0) {
        char text[64];
        lv_snprintf(text, sizeof(text), "Too many wrong codes. Try again in %u min", (unsigned)((locked_s + 59) / 60));
        ui_toast(ICON_ALERT, text);
    } else {
        ui_toast(ICON_ALERT, "The companion app didn't answer");
    }
    UNLOCK();
}

void ui_companion_pair_result(ui_pair_result_t result)
{
    LOCK();
    switch (result) {
    case UI_PAIR_OK:
        ui_show_view(VIEW_SETTINGS);
        ui_toast(ICON_CHECK, "Companion app paired");
        break;
    case UI_PAIR_WRONG:
        ui_setup_show_code_wrong();
        break;
    case UI_PAIR_EXPIRED:
        ui_setup_show_code_message("That code is no longer on the TV. Show a new one.");
        break;
    case UI_PAIR_FAILED:
        ui_setup_show_code_message("The companion app didn't answer. Try again.");
        break;
    }
    UNLOCK();
}

void ui_show_appearance(void)
{
    LOCK();
    ui_settings_show_appearance();
    UNLOCK();
}

/* --- IR devices ----------------------------------------------------------- */

static bool s_ir_send = true;
static bool s_ir_learn = true;

bool ui_ir_can_send(void)
{
    return s_ir_send;
}

bool ui_ir_can_learn(void)
{
    return s_ir_learn;
}

void ui_set_ir_caps(bool send, bool learn)
{
    LOCK();
    s_ir_send = send;
    s_ir_learn = send && learn;
    ui_settings_ir_caps_changed();
    ui_ir_caps_changed();
    ui_tv_setup_ir_caps_changed();
    ui_tv_home_ir_caps_changed();
    UNLOCK();
}

void ui_set_board_caps(bool dims, bool battery)
{
    LOCK();
    ui_settings_board_caps(dims, battery);
    ui_topbar_show_battery(battery);
    UNLOCK();
}

void ui_set_ir_remotes(const char *const *names, size_t count)
{
    LOCK();
    ui_settings_set_remotes(names, count);
    ui_learn_remotes_changed();
    UNLOCK();
}

void ui_set_ir_tv(const char *name, const char *detail, const char *const *keys, size_t count, bool volume_ir)
{
    LOCK();
    ui_ir_tv_update(name, detail, keys, count, volume_ir);
    ui_settings_tv_changed();
    if (s_view == VIEW_IR_TV && name && name[0]) {
        ui_topbar_set_target(name);
        ui_topbar_set_context(name);
    }
    UNLOCK();
}

void ui_set_tv_brands(const char *const *brands, size_t count)
{
    LOCK();
    ui_tv_setup_set_brands(brands, count);
    UNLOCK();
}

void ui_tv_setup_sets(const char *brand, const char *const *test_keys, size_t count)
{
    LOCK();
    ui_tv_setup_show_sets(brand, test_keys, count);
    UNLOCK();
}

void ui_show_ir_tv(void)
{
    LOCK();
    ui_irtv_show();
    UNLOCK();
}

void ui_set_inputs(const char *const *names, const bool *learned)
{
    LOCK();
    ui_inputs_set(names, learned);
    UNLOCK();
}

void ui_show_inputs(void)
{
    LOCK();
    ui_inputs_show();
    UNLOCK();
}

void ui_show_learned_remote(int index, const char *name, const ui_ir_button_t *buttons, size_t count)
{
    LOCK();
    ui_ir_remote_set_index(index);
    ui_ir_remote_set(name, buttons, count);
    ui_show_ir_view(VIEW_IR_REMOTE, name);
    UNLOCK();
}

void ui_learned_remote_deleted(void)
{
    LOCK();
    ui_ir_remote_set_index(-1);
    if (s_remote_view == VIEW_IR_REMOTE) {
        s_remote_view = VIEW_TV_HOME; /* the remote tab goes back to the Streamer */
    }
    ui_topbar_set_target(s_tv_name);
    ui_topbar_set_ir(false);
    ui_settings_show_remotes();
    UNLOCK();
}

void ui_notify(const char *icon, const char *text)
{
    LOCK();
    ui_toast(icon, text);
    UNLOCK();
}

void ui_learn_progress(int presses, int needed, bool mismatch)
{
    LOCK();
    ui_learn_show_progress(presses, needed, mismatch);
    UNLOCK();
}

void ui_learn_result(const char *summary)
{
    LOCK();
    ui_learn_show_result(summary);
    UNLOCK();
}

void ui_learn_failed(const char *reason)
{
    LOCK();
    ui_learn_show_failed(reason);
    UNLOCK();
}
