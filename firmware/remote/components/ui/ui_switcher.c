/*
 * The switcher: a sheet under the remote tab listing every remote and
 * module, plus Settings. Rebuilt each time it opens, so it always shows the
 * current learned remotes, modules and glances.
 */
#include "hub_module.h"
#include "ui_internal.h"

#define SHEET_W   UI_S(440)
#define ROW_H     UI_S(48)

static lv_obj_t *s_scrim;
static lv_obj_t *s_sheet;

typedef enum { ROW_TV, ROW_IR_TV, ROW_LEARNED, ROW_INPUTS, ROW_ADD_REMOTE, ROW_MODULE, ROW_SETTINGS } row_kind_t;

void ui_switcher_close(void)
{
    lv_obj_set_hidden(s_scrim, true);
}

bool ui_switcher_is_open(void)
{
    return s_scrim && !lv_obj_is_hidden(s_scrim);
}

static void on_scrim(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_scrim) {
        ui_switcher_close();
    }
}

static void on_row(lv_event_t *e)
{
    uint32_t data = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    row_kind_t kind = (row_kind_t)(data >> 16);
    int index = (int)(data & 0xFFFF);
    ui_switcher_close();
    switch (kind) {
    case ROW_TV:
        ui_return_to_context();
        break;
    case ROW_IR_TV:
        if (ui_irtv_configured()) {
            ui_irtv_show();
        } else {
            ui_tv_setup_open();
        }
        break;
    case ROW_LEARNED:
        ui_settings_open_remote(index);
        break;
    case ROW_INPUTS:
        ui_inputs_show();
        break;
    case ROW_ADD_REMOTE:
        ui_learn_open(-1);
        ui_show_view(VIEW_LEARN);
        break;
    case ROW_MODULE: {
        const hub_module_t *m = hub_module_get((size_t)index);
        if (m) {
            ui_module_open(m->id);
        }
        break;
    }
    case ROW_SETTINGS:
        ui_show_view(VIEW_SETTINGS);
        break;
    }
}

static void section(const char *title)
{
    lv_obj_t *l = ui_label(s_sheet, &font_sora_11, COL_TEXT2, title);
    lv_obj_set_style_pad_left(l, UI_S(8), 0);
    lv_obj_set_style_pad_top(l, UI_S(6), 0);
    lv_obj_set_style_pad_bottom(l, UI_S(2), 0);
}

static void row(row_kind_t kind, int index, const char *icon, lv_color_t icon_color, const char *name,
                const char *meta, bool selected)
{
    lv_obj_t *r = ui_btn(s_sheet, LV_PCT(100), ROW_H, 12, selected ? COL_SURFACE3 : COL_SURFACE1);
    lv_obj_set_style_pad_hor(r, UI_S(8), 0);
    ui_flex(r, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(r, on_row, LV_EVENT_CLICKED, (void *)(uintptr_t)((uint32_t)kind << 16 | (uint32_t)index));
    lv_obj_t *box = ui_box(r);
    lv_obj_set_size(box, UI_S(34), UI_S(34));
    lv_obj_set_style_radius(box, ui_radius(10), 0);
    lv_obj_set_style_bg_color(box, COL_SURFACE3, 0);
    lv_obj_set_style_bg_opa(box, icon ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_center(ui_label(box, &font_icons_20, icon_color, icon ? icon : ICON_PLUS));
    lv_obj_t *n = ui_label(r, &font_sora_15m, icon ? COL_TEXT : COL_TEXT2, name);
    lv_obj_set_flex_grow(n, 1);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    if (meta && meta[0]) {
        ui_label(r, &font_mono_12, COL_TEXT2, meta);
    }
}

void ui_switcher_open(void)
{
    lv_obj_clean(s_sheet);
    ui_view_t view = ui_current_view();

    section("REMOTES");
    bool on_tv = view == VIEW_TV_HOME || view == VIEW_APP;
    row(ROW_TV, 0, ICON_STREAMER, COL_ACCENT_TEXT, ui_tv_display_name(), ui_tv_meta(), on_tv);
    if (ui_irtv_configured() && ui_ir_can_send()) {
        row(ROW_IR_TV, 0, ICON_TV, COL_IR, ui_irtv_name(), "IR", view == VIEW_IR_TV);
    } else if (ui_ir_can_send()) {
        row(ROW_IR_TV, 0, NULL, COL_TEXT2, "Add a TV", NULL, view == VIEW_TV_SETUP);
    }
    for (size_t i = 0; ui_ir_can_send() && i < ui_settings_remote_count(); i++) {
        bool sel = view == VIEW_IR_REMOTE && ui_ir_remote_index() == (int)i;
        row(ROW_LEARNED, (int)i, ICON_IR, COL_IR, ui_settings_remote_name(i), "IR", sel);
    }
    if (ui_ir_can_learn()) {
        row(ROW_INPUTS, 0, ICON_INPUTS, COL_IR, "HDMI switch", "IR", view == VIEW_INPUTS);
        row(ROW_ADD_REMOTE, 0, NULL, COL_TEXT2, "Add a remote", NULL, false);
    }

    bool header = false;
    for (size_t i = 0; i < hub_module_count(); i++) {
        const hub_module_t *m = hub_module_get(i);
        if (!hub_module_enabled(m)) {
            continue;
        }
        if (!header) {
            section("MODULES");
            header = true;
        }
        hub_glance_t g = {0};
        bool has = m->glance && m->glance(&g);
        bool sel = view == VIEW_MODULE && ui_modview_current() == m;
        row(ROW_MODULE, (int)i, m->icon ? m->icon : ICON_GRID, has ? g.color : COL_TEXT2, m->name,
            has ? g.text : NULL, sel);
    }

    lv_obj_t *line = ui_box(s_sheet);
    lv_obj_set_size(line, LV_PCT(100), UI_S(1));
    lv_obj_set_style_bg_color(line, COL_LINE, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_ver(line, UI_S(4), 0);
    row(ROW_SETTINGS, 0, ICON_GEAR, COL_TEXT2, "Settings", NULL, view == VIEW_SETTINGS);

    lv_obj_scroll_to_y(s_sheet, 0, LV_ANIM_OFF);
    lv_obj_set_hidden(s_scrim, false);
}

void ui_switcher_build(void)
{
    s_scrim = ui_box(lv_layer_top());
    lv_obj_set_size(s_scrim, UI_W, UI_H - UI_TOPBAR_H);
    lv_obj_set_pos(s_scrim, 0, UI_TOPBAR_H);
    lv_obj_set_style_bg_color(s_scrim, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_60, 0);
    lv_obj_set_clickable(s_scrim, true);
    lv_obj_add_event_cb(s_scrim, on_scrim, LV_EVENT_CLICKED, NULL);

    s_sheet = ui_box(s_scrim);
    lv_obj_set_size(s_sheet, SHEET_W, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_sheet, UI_BODY_H, 0);
    lv_obj_set_pos(s_sheet, UI_GAP, UI_S(4));
    lv_obj_set_style_bg_color(s_sheet, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_sheet, UI_S(1), 0);
    lv_obj_set_style_border_color(s_sheet, COL_LINE2, 0);
    lv_obj_set_style_radius(s_sheet, ui_radius(20), 0);
    lv_obj_set_style_pad_all(s_sheet, UI_S(12), 0);
    lv_obj_set_clickable(s_sheet, true); /* taps between rows don't close it */
    lv_obj_set_scrollable(s_sheet, true);
    lv_obj_set_scroll_dir(s_sheet, LV_DIR_VER);
    ui_flex(s_sheet, LV_FLEX_FLOW_COLUMN, UI_S(2));
    lv_obj_set_hidden(s_scrim, true);
}
