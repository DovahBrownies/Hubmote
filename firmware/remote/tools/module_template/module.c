/*
 * __NAME__ module. Made from tools/module_template by tools/new_module.py;
 * see docs/modules.md.
 *
 * What it shows as a starting point:
 *  - a Home card and a full screen built from the UI kit (ui_kit.h),
 *    with sizes in UI_S() so they follow the screen's size;
 *  - a value kept in NVS (hub_kv_*), here a tap counter;
 *  - a setting in Settings > Modules (hub_setting_t), here how much a tap
 *    adds;
 *  - reacting to events (screen on/off, network, once a minute);
 *  - where a web request would go (hub_http_get), commented out; while
 *    waiting, show ui_status (loading, error, empty: see docs/modules.md).
 * modules/weather is a complete example, with a glance and a host test.
 * Replace what you don't need.
 */
#include <stdio.h>
#include "hub.h"
#include "ui_kit.h"

#define MODULE_ID "__ID__"

static int32_t s_taps;       /* kept in NVS: survives restarts */
static lv_obj_t *s_card_value;
static lv_obj_t *s_view_value;

/* --- UI (LVGL task) ------------------------------------------------------- */

static void show_value(void *arg)
{
    if (s_card_value) {
        lv_label_set_text_fmt(s_card_value, "%ld", (long)s_taps);
    }
    if (s_view_value) {
        lv_label_set_text_fmt(s_view_value, "%ld", (long)s_taps);
    }
}

/* Settings > Modules shows these under the module (docs/modules.md). */
static const hub_setting_t k_settings[] = {
    {.key = "step", .label = "Each tap adds", .type = HUB_SETTING_NUMBER, .def = 1, .min = 1, .max = 10, .step = 1},
};

static void on_tap(lv_event_t *e)
{
    s_taps += hub_setting_i32(hub_module_find(MODULE_ID), "step");
    hub_kv_set_i32(MODULE_ID, "taps", s_taps);
    show_value(NULL);
}

/* Its card on the Home tab. The Home tab makes the whole card open the
 * module's screen when tapped. */
static lv_obj_t *create_card(lv_obj_t *parent)
{
    lv_obj_t *card = ui_panel(parent, UI_S(480), UI_S(160));
    ui_flex(card, LV_FLEX_FLOW_COLUMN, UI_S(6));
    ui_label(card, &font_sora_16sb, COL_TEXT, "__NAME__");
    s_card_value = ui_label(card, &font_sora_26m, COL_ACCENT_TEXT, "");
    show_value(NULL);
    return card;
}

/* Its screen: HUB_VIEW_W x HUB_VIEW_H, below the top bar. */
static lv_obj_t *create_view(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, HUB_VIEW_W, HUB_VIEW_H);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(16));
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_view_value = ui_label(p, &font_sora_88n, COL_TEXT, "");
    lv_obj_t *b = ui_btn_icon(p, UI_S(200), UI_S(56), 16, COL_ACCENT, &font_icons_20, ICON_PLUS, COL_ON_ACCENT, "Tap", false);
    lv_obj_add_event_cb(b, on_tap, LV_EVENT_CLICKED, NULL);
    show_value(NULL);
    return p;
}

/* --- Logic (controller task) ---------------------------------------------- */

/*
static void on_answer(int status, const char *body, size_t len, void *ctx)
{
    // Runs on the HTTP task: parse here, then hand the UI work over.
    hub_ui_async(show_value, NULL);
}
*/

static void init(void)
{
    hub_kv_get_i32(MODULE_ID, "taps", &s_taps);
}

static void event(hub_event_t ev)
{
    switch (ev) {
    case HUB_EV_NETWORK_UP:
        /* hub_http_get("https://example.com/data.json", 8192, on_answer, NULL); */
        break;
    case HUB_EV_MINUTE:
    case HUB_EV_SCREEN_ON:
    case HUB_EV_SCREEN_OFF:
    case HUB_EV_NETWORK_DOWN:
    case HUB_EV_NOW_PLAYING:
        break;
    }
}

static const hub_module_t s_module = {
    .id = MODULE_ID,
    .name = "__NAME__",
    .icon = ICON_GRID,
    .init = init,
    .event = event,
    .create_view = create_view,
    .create_card = create_card,
    /* .glance: a short summary for the top bar and switcher (see Weather) */
    .settings = k_settings,
    .settings_count = sizeof(k_settings) / sizeof(k_settings[0]),
    /* .setting_changed: called when one changes (see Weather) */
};
HUB_MODULE(s_module);
