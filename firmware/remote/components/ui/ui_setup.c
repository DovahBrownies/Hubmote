/*
 * First run and pairing: one wide panel with five states (choose a Wi-Fi
 * network, busy, choose a TV, enter the code the TV shows, error). Only one
 * is visible at a time. The code entry also pairs the companion app, opened
 * from Settings: then it has Cancel, and its actions go to the companion.
 */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

#define CODE_LEN 6

static lv_obj_t *s_busy;
static lv_obj_t *s_busy_title;
static lv_obj_t *s_busy_body;
static lv_obj_t *s_tvs;
static lv_obj_t *s_tv_list;
static lv_obj_t *s_tv_empty;
static lv_obj_t *s_code;
static lv_obj_t *s_code_title;
static lv_obj_t *s_code_msg;
static lv_obj_t *s_code_cancel;
static bool s_companion; /* the code entry is for the companion app */
static lv_obj_t *s_slots[CODE_LEN];
static lv_obj_t *s_slot_labels[CODE_LEN];
static lv_obj_t *s_error;
static lv_obj_t *s_error_title;
static lv_obj_t *s_error_body;
static lv_obj_t *s_wifi;

static char s_entry[CODE_LEN + 1];
static ui_tv_t s_tv_copy[8];

static void show_only(lv_obj_t *which)
{
    lv_obj_t *all[] = {s_busy, s_tvs, s_code, s_error, s_wifi};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        lv_obj_set_hidden(all[i], all[i] != which);
    }
}

static lv_obj_t *section(lv_obj_t *parent)
{
    lv_obj_t *s = ui_box(parent);
    lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
    ui_flex(s, LV_FLEX_FLOW_COLUMN, UI_S(12));
    return s;
}

/* --- Code entry ----------------------------------------------------------- */

static void refresh_slots(void)
{
    size_t n = strlen(s_entry);
    for (int i = 0; i < CODE_LEN; i++) {
        char s[2] = {i < (int)n ? s_entry[i] : '\0', '\0'};
        lv_label_set_text(s_slot_labels[i], s);
        lv_obj_set_style_border_color(s_slots[i], i == (int)n ? COL_ACCENT_TEXT : COL_SURFACE2, 0);
    }
}

static void on_hex(lv_event_t *e)
{
    const char *ch = lv_event_get_user_data(e);
    size_t n = strlen(s_entry);
    if (n < CODE_LEN) {
        s_entry[n] = ch[0];
        s_entry[n + 1] = '\0';
    }
    lv_label_set_text(s_code_msg, "");
    refresh_slots();
}

static void on_code_delete(lv_event_t *e)
{
    size_t n = strlen(s_entry);
    if (n) {
        s_entry[n - 1] = '\0';
    }
    refresh_slots();
}

static void on_code_ok(lv_event_t *e)
{
    if (strlen(s_entry) != CODE_LEN) {
        lv_obj_set_style_text_color(s_code_msg, COL_ACCENT_TEXT, 0);
        lv_label_set_text(s_code_msg, "The code has 6 characters");
        return;
    }
    lv_obj_set_style_text_color(s_code_msg, COL_TEXT2, 0);
    lv_label_set_text(s_code_msg, "Checking\xE2\x80\xA6");
    ui_action_t a = {.type = s_companion ? UI_ACT_COMPANION_CODE : UI_ACT_PAIR_CODE, .text = s_entry};
    ui_emit(&a);
}

static void on_code_again(lv_event_t *e)
{
    ui_action_t a = {.type = s_companion ? UI_ACT_COMPANION_PAIR : UI_ACT_PAIR_RESTART};
    ui_emit(&a);
}

static void on_code_cancel(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_COMPANION_CANCEL};
    ui_emit(&a);
    ui_show_view(VIEW_SETTINGS);
}

static void build_code(lv_obj_t *panel)
{
    s_code = section(panel);
    s_code_title = ui_label(s_code, &font_sora_20sb, COL_TEXT, "");
    ui_label(s_code, &font_sora_12, COL_TEXT2, "6 characters: numbers 0\xE2\x80\x93" "9 and letters A\xE2\x80\x93" "F");

    lv_obj_t *slots = ui_box(s_code);
    lv_obj_set_size(slots, LV_PCT(100), UI_S(72));
    lv_obj_set_style_pad_top(slots, UI_S(4), 0);
    ui_flex(slots, LV_FLEX_FLOW_ROW, UI_S(10));
    for (int i = 0; i < CODE_LEN; i++) {
        s_slots[i] = ui_box(slots);
        lv_obj_set_size(s_slots[i], UI_S(72), UI_S(68));
        lv_obj_set_style_radius(s_slots[i], ui_radius(14), 0);
        lv_obj_set_style_bg_color(s_slots[i], COL_SURFACE2, 0);
        lv_obj_set_style_bg_opa(s_slots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_slots[i], UI_S(2), 0);
        s_slot_labels[i] = ui_label(s_slots[i], &font_sora_26m, COL_TEXT, "");
        lv_obj_center(s_slot_labels[i]);
    }
    s_code_msg = ui_label(s_code, &font_sora_13m, COL_TEXT2, "");

    static const char HEX[] = "0123456789ABCDEF";
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *grid = ui_box(s_code);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_set_style_pad_row(grid, UI_S(10), 0);
    lv_obj_set_style_pad_column(grid, UI_S(10), 0);
    for (int i = 0; i < 16; i++) {
        lv_obj_t *b = ui_btn(grid, LV_PCT(100), LV_PCT(100), 14, COL_SURFACE2);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 6, 1, LV_GRID_ALIGN_STRETCH, i / 6, 1);
        char s[2] = {HEX[i], '\0'};
        lv_obj_center(ui_label(b, &font_sora_22m, COL_TEXT, s));
        lv_obj_add_event_cb(b, on_hex, LV_EVENT_CLICKED, (void *)&HEX[i]);
    }
    lv_obj_t *del = ui_btn(grid, LV_PCT(100), LV_PCT(100), 14, COL_SURFACE3);
    lv_obj_set_grid_cell(del, LV_GRID_ALIGN_STRETCH, 4, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_center(ui_label(del, &font_icons_24, COL_TEXT, ICON_BACKSPACE));
    lv_obj_add_event_cb(del, on_code_delete, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok = ui_btn(grid, LV_PCT(100), LV_PCT(100), 14, COL_ACCENT);
    lv_obj_set_grid_cell(ok, LV_GRID_ALIGN_STRETCH, 5, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_center(ui_label(ok, &font_sora_16sb, COL_ON_ACCENT, "Pair"));
    lv_obj_add_event_cb(ok, on_code_ok, LV_EVENT_CLICKED, NULL);

    lv_obj_t *foot = ui_box(s_code);
    lv_obj_set_size(foot, LV_PCT(100), UI_S(40));
    ui_flex(foot, LV_FLEX_FLOW_ROW, UI_S(10));
    lv_obj_t *again = ui_btn_icon(foot, LV_SIZE_CONTENT, UI_S(40), 10, COL_SURFACE1, &font_icons_16, ICON_REFRESH,
                                  COL_TEXT2, "Show a new code on the TV", false);
    lv_obj_set_style_pad_hor(again, UI_S(12), 0);
    lv_obj_add_event_cb(again, on_code_again, LV_EVENT_CLICKED, NULL);
    s_code_cancel = ui_btn_icon(foot, LV_SIZE_CONTENT, UI_S(40), 10, COL_SURFACE1, &font_icons_16, ICON_CLOSE, COL_TEXT2,
                                "Cancel", false);
    lv_obj_set_style_pad_hor(s_code_cancel, UI_S(12), 0);
    lv_obj_add_event_cb(s_code_cancel, on_code_cancel, LV_EVENT_CLICKED, NULL);
}

/* --- TV list -------------------------------------------------------------- */

static void build_tvs(lv_obj_t *panel)
{
    s_tvs = section(panel);
    ui_label(s_tvs, &font_sora_20sb, COL_TEXT, "Choose your Google TV");
    s_tv_list = ui_box(s_tvs);
    lv_obj_set_width(s_tv_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_tv_list, 1);
    ui_flex(s_tv_list, LV_FLEX_FLOW_COLUMN, UI_S(10));
    s_tv_empty = ui_label_wrap(s_tvs, &font_sora_15m, COL_TEXT2,
                         "No Google TV found. Check that it is on and on the same Wi-Fi network.");
    lv_obj_t *again = ui_btn_icon(s_tvs, LV_SIZE_CONTENT, UI_S(48), 12, COL_SURFACE2, &font_icons_20, ICON_REFRESH,
                                  COL_TEXT, "Search again", false);
    lv_obj_set_style_pad_hor(again, UI_S(16), 0);
    ui_btn_bind_action(again, UI_ACT_RESCAN, NULL, 0);
}

/* --- Busy and error ------------------------------------------------------- */

static void build_busy(lv_obj_t *panel)
{
    s_busy = section(panel);
    lv_obj_set_flex_align(s_busy, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_busy, UI_S(16), 0);
    ui_spinner(s_busy, UI_S(56), COL_ACCENT_TEXT);
    s_busy_title = ui_label(s_busy, &font_sora_20sb, COL_TEXT, "");
    s_busy_body = ui_label(s_busy, &font_sora_15m, COL_TEXT2, "");
    lv_obj_set_width(s_busy_body, UI_S(560));
    lv_label_set_long_mode(s_busy_body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_busy_body, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_error(lv_obj_t *panel)
{
    s_error = section(panel);
    lv_obj_set_flex_align(s_error, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_error, UI_S(16), 0);
    ui_label(s_error, &font_icons_28, COL_ACCENT_TEXT, ICON_ALERT);
    s_error_title = ui_label(s_error, &font_sora_20sb, COL_TEXT, "");
    s_error_body = ui_label(s_error, &font_sora_15m, COL_TEXT2, "");
    lv_obj_set_width(s_error_body, UI_S(560));
    lv_label_set_long_mode(s_error_body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_error_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *retry = ui_btn_text(s_error, UI_S(200), UI_S(52), 14, COL_ACCENT, &font_sora_15m, COL_ON_ACCENT, "Try again");
    ui_btn_bind_action(retry, UI_ACT_RETRY, NULL, 0);
}

static void build_wifi(lv_obj_t *panel)
{
    s_wifi = section(panel);
    ui_label(s_wifi, &font_sora_20sb, COL_TEXT, "Choose your Wi-Fi");
    ui_wifi_picker_create(s_wifi, UI_WIFI_SETUP, VIEW_SETUP);
}

lv_obj_t *ui_setup_create(lv_obj_t *parent)
{
    lv_obj_t *panel = ui_panel(parent, UI_STAGE_W, UI_BODY_H);
    build_wifi(panel);
    build_busy(panel);
    build_tvs(panel);
    build_code(panel);
    build_error(panel);
    show_only(s_busy);
    return panel;
}

void ui_setup_show_busy(const char *title, const char *body)
{
    lv_label_set_text(s_busy_title, title);
    lv_label_set_text(s_busy_body, body ? body : "");
    show_only(s_busy);
}

void ui_setup_show_tvs(const ui_tv_t *tvs, size_t count)
{
    lv_obj_clean(s_tv_list);
    if (count > sizeof(s_tv_copy) / sizeof(s_tv_copy[0])) {
        count = sizeof(s_tv_copy) / sizeof(s_tv_copy[0]);
    }
    for (size_t i = 0; i < count; i++) {
        s_tv_copy[i] = tvs[i];
        lv_obj_t *row = ui_btn(s_tv_list, LV_PCT(100), UI_S(64), 16, COL_SURFACE2);
        lv_obj_set_style_pad_hor(row, UI_S(16), 0);
        ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(14));
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label(row, &font_icons_24, COL_ACCENT_TEXT, ICON_STREAMER);
        lv_obj_t *name = ui_label(row, &font_sora_15m, COL_TEXT, s_tv_copy[i].name);
        lv_obj_set_flex_grow(name, 1);
        ui_label(row, &font_mono_12, COL_TEXT3, s_tv_copy[i].host);
        ui_label(row, &font_icons_20, COL_TEXT3, ICON_CHEVRON_RIGHT);
        ui_btn_bind_action(row, UI_ACT_PICK_TV, NULL, (int)i);
    }
    lv_obj_set_hidden(s_tv_empty, count != 0);
    show_only(s_tvs);
}

static void show_code(bool companion, const char *title)
{
    s_companion = companion;
    lv_label_set_text(s_code_title, title);
    lv_obj_set_hidden(s_code_cancel, !companion);
    s_entry[0] = '\0';
    lv_label_set_text(s_code_msg, "");
    refresh_slots();
    show_only(s_code);
}

void ui_setup_show_code(const char *tv_name)
{
    char title[96];
    snprintf(title, sizeof(title), "Enter the code shown on %s", (tv_name && tv_name[0]) ? tv_name : "your TV");
    show_code(false, title);
}

void ui_setup_show_companion_code(void)
{
    show_code(true, "Enter the companion app's code from the TV");
}

void ui_setup_show_code_message(const char *message)
{
    s_entry[0] = '\0';
    refresh_slots();
    lv_obj_set_style_text_color(s_code_msg, COL_ACCENT_TEXT, 0);
    lv_label_set_text(s_code_msg, message);
}

void ui_setup_show_code_wrong(void)
{
    ui_setup_show_code_message("That code didn't match. Check the TV and try again.");
}

void ui_setup_show_error(const char *title, const char *body)
{
    lv_label_set_text(s_error_title, title);
    lv_label_set_text(s_error_body, body ? body : "");
    show_only(s_error);
}

void ui_setup_show_wifi(const char *message)
{
    ui_wifi_picker_message(UI_WIFI_SETUP, message);
    show_only(s_wifi);
    ui_wifi_scan();
}
