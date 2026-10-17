/*
 * Add a TV (VIEW_TV_SETUP, full width): pick the brand, then try its
 * built-in code sets one at a time until the TV reacts, or learn the keys
 * from the TV's own remote. Also the TV's key list, where a missing key
 * can be learned (docs/ir-tvs.md).
 */
#include <string.h>
#include "ui_internal.h"

#define BRAND_W    ((UI_WIDE_INNER_W - 3 * UI_S(12)) / 4) /* four across, UI_S(12) apart */
#define KEY_W      ((UI_WIDE_INNER_W - 7 * UI_S(10)) / 8) /* eight across, UI_S(10) apart */

static const struct {
    const char *key;
    const char *label;
} k_keys[] = {
    {"power", "Power"},      {"power_on", "Power on"},   {"power_off", "Power off"}, {"vol_up", "Volume up"},
    {"vol_down", "Volume down"}, {"mute", "Mute"},       {"ch_up", "Channel up"},    {"ch_down", "Channel down"},
    {"source", "Source"},    {"hdmi1", "HDMI 1"},        {"hdmi2", "HDMI 2"},        {"hdmi3", "HDMI 3"},
    {"hdmi4", "HDMI 4"},     {"tv", "TV"},               {"up", "Up"},               {"down", "Down"},
    {"left", "Left"},        {"right", "Right"},         {"enter", "OK"},            {"return", "Back"},
    {"home", "Home"},        {"menu", "Menu"},           {"settings", "Settings"},   {"info", "Info"},
    {"exit", "Exit"},        {"play", "Play"},           {"pause", "Pause"},         {"0", "0"},
    {"1", "1"},              {"2", "2"},                 {"3", "3"},                 {"4", "4"},
    {"5", "5"},              {"6", "6"},                 {"7", "7"},                 {"8", "8"},
    {"9", "9"},
};
#define KEY_COUNT (sizeof(k_keys) / sizeof(k_keys[0]))

static lv_obj_t *s_brands_step;
static lv_obj_t *s_test_step;
static lv_obj_t *s_keys_step;
static lv_obj_t *s_brand_grid;
static lv_obj_t *s_remove;
static lv_obj_t *s_test_title;
static lv_obj_t *s_send_label;
static lv_obj_t *s_next;
static lv_obj_t *s_test_learn;
static lv_obj_t *s_keys_title;
static lv_obj_t *s_key_btn[KEY_COUNT];

static char s_brands[UI_TV_BRANDS_MAX][24];
static size_t s_brand_count;
static char s_brand[24];
static char s_test_keys[UI_TV_SETS_MAX][12];
static size_t s_set_count;
static size_t s_set;

const char *ui_irtv_key_label(const char *key)
{
    for (size_t i = 0; key && i < KEY_COUNT; i++) {
        if (strcmp(k_keys[i].key, key) == 0) {
            return k_keys[i].label;
        }
    }
    return key ? key : "";
}

/* --- Steps ------------------------------------------------------------------- */

static void show_step(lv_obj_t *step)
{
    lv_obj_set_hidden(s_brands_step, step != s_brands_step);
    lv_obj_set_hidden(s_test_step, step != s_test_step);
    lv_obj_set_hidden(s_keys_step, step != s_keys_step);
}

static lv_obj_t *step_box(lv_obj_t *parent)
{
    lv_obj_t *s = ui_box(parent);
    lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
    ui_flex(s, LV_FLEX_FLOW_COLUMN, UI_S(16));
    return s;
}

/* A title on the left, a small button on the right. */
static lv_obj_t *step_head(lv_obj_t *step, const char *title, const char *button, lv_event_cb_t cb)
{
    lv_obj_t *head = ui_box(step);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *t = ui_label(head, &font_sora_20sb, COL_TEXT, title);
    lv_obj_t *b = ui_btn_text(head, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE2, &font_sora_12, COL_TEXT2, button);
    lv_obj_set_style_pad_hor(b, UI_S(14), 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return t;
}

static lv_obj_t *spacer(lv_obj_t *parent)
{
    lv_obj_t *s = ui_box(parent);
    lv_obj_set_width(s, UI_S(1));
    lv_obj_set_flex_grow(s, 1);
    return s;
}

static lv_obj_t *ir_outline_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *b = ui_btn_icon(parent, LV_SIZE_CONTENT, UI_S(56), 16, COL_SURFACE1, &font_icons_20, ICON_IR, COL_IR, text,
                              false);
    lv_obj_set_style_pad_hor(b, UI_S(24), 0);
    lv_obj_set_style_border_width(b, UI_S(2), 0);
    lv_obj_set_style_border_color(b, COL_IR_LINE, 0);
    return b;
}

/* --- Brands ------------------------------------------------------------------- */

static void on_cancel(lv_event_t *e)
{
    if (ui_irtv_configured()) {
        ui_irtv_show();
    } else {
        ui_remote_return();
    }
}

static void on_brand(lv_event_t *e)
{
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    ui_action_t a = {.type = UI_ACT_TV_BRAND, .text = s_brands[i]};
    ui_emit(&a);
}

static void on_learned(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_TV_LEARNED};
    ui_emit(&a);
}

static void on_remove(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_TV_REMOVE};
    ui_emit(&a);
    ui_show_view(VIEW_SETTINGS);
}

static lv_obj_t *s_brands_learn;

static void build_brands(lv_obj_t *panel)
{
    s_brands_step = step_box(panel);
    step_head(s_brands_step, "TV brand", "Cancel", on_cancel);
    s_brand_grid = ui_box(s_brands_step);
    lv_obj_set_size(s_brand_grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_brand_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_brand_grid, UI_S(12), 0);
    lv_obj_set_style_pad_column(s_brand_grid, UI_S(12), 0);
    spacer(s_brands_step);
    lv_obj_t *row = ui_box(s_brands_step);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_GAP);
    s_brands_learn = ir_outline_btn(row, "Learn from its remote");
    lv_obj_add_event_cb(s_brands_learn, on_learned, LV_EVENT_CLICKED, NULL);
    s_remove = ui_btn_icon(row, LV_SIZE_CONTENT, UI_S(56), 16, COL_SURFACE2, &font_icons_20, ICON_CLOSE, COL_BAD,
                           "Remove TV", false);
    lv_obj_set_style_pad_hor(s_remove, UI_S(24), 0);
    lv_obj_add_event_cb(s_remove, on_remove, LV_EVENT_CLICKED, NULL);
}

void ui_tv_setup_set_brands(const char *const *brands, size_t count)
{
    lv_obj_clean(s_brand_grid);
    s_brand_count = 0;
    for (size_t i = 0; i < count && i < UI_TV_BRANDS_MAX; i++) {
        strlcpy(s_brands[i], brands[i], sizeof(s_brands[i]));
        lv_obj_t *b = ui_btn_text(s_brand_grid, BRAND_W, UI_S(56), 14, COL_SURFACE2, &font_sora_15m, COL_TEXT, s_brands[i]);
        lv_obj_add_event_cb(b, on_brand, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_brand_count++;
    }
}

void ui_tv_setup_open(void)
{
    lv_obj_set_hidden(s_remove, !ui_irtv_configured());
    show_step(s_brands_step);
    ui_topbar_set_context(ui_irtv_configured() ? "Change TV" : "Add a TV");
    ui_show_view(VIEW_TV_SETUP);
}

/* --- Trying code sets ------------------------------------------------------------ */

static void show_set(void)
{
    lv_label_set_text_fmt(s_test_title, "%s \xC2\xB7 code set %u of %u", s_brand, (unsigned)(s_set + 1),
                          (unsigned)s_set_count);
    lv_label_set_text_fmt(s_send_label, "Send %s", ui_irtv_key_label(s_test_keys[s_set]));
    bool last = s_set + 1 >= s_set_count;
    lv_obj_set_hidden(s_next, last);
    lv_obj_set_hidden(s_test_learn, !last);
}

static void on_brands(lv_event_t *e)
{
    show_step(s_brands_step);
}

static void on_send(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_TV_TRY, .index = (int)s_set};
    ui_emit(&a);
}

static void on_worked(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_TV_CHOOSE, .index = (int)s_set};
    ui_emit(&a);
}

static void on_next(lv_event_t *e)
{
    if (s_set + 1 < s_set_count) {
        s_set++;
        show_set();
    }
}

static void build_test(lv_obj_t *panel)
{
    s_test_step = step_box(panel);
    s_test_title = step_head(s_test_step, "", "Brands", on_brands);
    spacer(s_test_step);
    lv_obj_t *mid = ui_box(s_test_step);
    lv_obj_set_size(mid, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(mid, LV_FLEX_FLOW_COLUMN, UI_S(20));
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *send = ui_btn(mid, UI_S(360), UI_S(76), 18, COL_IR);
    ui_flex(send, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(send, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(send, &font_icons_24, COL_ON_IR, ICON_IR);
    s_send_label = ui_label(send, &font_sora_18b, COL_ON_IR, "");
    lv_obj_add_event_cb(send, on_send, LV_EVENT_CLICKED, NULL);
    ui_label(mid, &font_sora_20sb, COL_TEXT, "Did the TV react?");
    lv_obj_t *answers = ui_box(mid);
    lv_obj_set_size(answers, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(answers, LV_FLEX_FLOW_ROW, UI_GAP);
    lv_obj_t *yes = ui_btn_icon(answers, UI_S(220), UI_S(56), 16, COL_ACCENT, &font_icons_20, ICON_CHECK, COL_ON_ACCENT,
                                "It worked", false);
    lv_obj_add_event_cb(yes, on_worked, LV_EVENT_CLICKED, NULL);
    s_next = ui_btn_text(answers, UI_S(220), UI_S(56), 16, COL_SURFACE2, &font_sora_15m, COL_TEXT, "Next set");
    lv_obj_add_event_cb(s_next, on_next, LV_EVENT_CLICKED, NULL);
    s_test_learn = ir_outline_btn(answers, "Learn from its remote");
    lv_obj_add_event_cb(s_test_learn, on_learned, LV_EVENT_CLICKED, NULL);
    spacer(s_test_step);
}

void ui_tv_setup_ir_caps_changed(void)
{
    lv_obj_set_hidden(s_brands_learn, !ui_ir_can_learn());
    lv_obj_set_hidden(s_test_learn, !ui_ir_can_learn());
}

void ui_tv_setup_show_sets(const char *brand, const char *const *test_keys, size_t count)
{
    strlcpy(s_brand, brand ? brand : "", sizeof(s_brand));
    s_set_count = 0;
    for (size_t i = 0; i < count && i < UI_TV_SETS_MAX; i++) {
        strlcpy(s_test_keys[s_set_count++], test_keys[i], sizeof(s_test_keys[0]));
    }
    s_set = 0;
    if (s_set_count == 0) {
        show_step(s_brands_step);
        return;
    }
    show_set();
    show_step(s_test_step);
}

/* --- Keys -------------------------------------------------------------------------- */

static void on_keys_done(lv_event_t *e)
{
    ui_irtv_show();
}

static void on_key(lv_event_t *e)
{
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    ui_learn_open_tv_key(k_keys[i].key);
}

static void build_keys(lv_obj_t *panel)
{
    s_keys_step = step_box(panel);
    s_keys_title = step_head(s_keys_step, "", "Done", on_keys_done);
    lv_obj_t *grid = ui_box(s_keys_step);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, UI_S(10), 0);
    lv_obj_set_style_pad_column(grid, UI_S(10), 0);
    for (size_t i = 0; i < KEY_COUNT; i++) {
        lv_obj_t *b = s_key_btn[i] = ui_btn(grid, KEY_W, UI_S(52), 12, COL_SURFACE2);
        lv_obj_t *l = ui_label(b, &font_sora_13m, COL_TEXT, k_keys[i].label);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_key, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

/* Keys the TV has look like buttons; missing ones are outlined. Any of
 * them can be (re)learned. */
void ui_tv_setup_open_keys(void)
{
    lv_label_set_text_fmt(s_keys_title, "%s keys", ui_irtv_configured() ? ui_irtv_name() : "TV");
    for (size_t i = 0; i < KEY_COUNT; i++) {
        bool have = ui_irtv_has(k_keys[i].key);
        lv_obj_set_style_bg_opa(s_key_btn[i], have ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_key_btn[i], have ? 0 : UI_S(1), 0);
        lv_obj_set_style_border_color(s_key_btn[i], COL_LINE2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_key_btn[i], 0), have ? COL_TEXT : COL_TEXT3, 0);
    }
    show_step(s_keys_step);
    ui_topbar_set_context("TV keys");
    ui_show_view(VIEW_TV_SETUP);
}

lv_obj_t *ui_tv_setup_create(lv_obj_t *parent)
{
    lv_obj_t *panel = ui_panel(parent, UI_WIDE_W, UI_BODY_H);
    build_brands(panel);
    build_test(panel);
    build_keys(panel);
    show_step(s_brands_step);
    return panel;
}
