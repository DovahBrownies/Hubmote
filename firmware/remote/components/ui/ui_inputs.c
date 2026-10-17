/*
 * Inputs page (Home → Inputs): the HDMI switch's inputs, up to UI_INPUTS,
 * each named and learned from the switch's own remote. A tap sends that
 * input's IR code; tapping one not learned yet starts learning it. Edit
 * shows Rename and Learn on every input.
 */
#include <stdint.h>
#include <string.h>
#include "ui_internal.h"


static lv_obj_t *s_name[UI_INPUTS];
static lv_obj_t *s_status[UI_INPUTS];
static lv_obj_t *s_tools[UI_INPUTS];
static lv_obj_t *s_edit_btn;
static lv_obj_t *s_edit_label;
static char s_names[UI_INPUTS][UI_INPUT_NAME_MAX];
static bool s_learned[UI_INPUTS];
static bool s_editing;
static int s_renaming = -1;

static void refresh(void)
{
    for (int i = 0; i < UI_INPUTS; i++) {
        lv_label_set_text(s_name[i], s_names[i]);
        lv_obj_set_style_text_color(s_name[i], s_learned[i] ? COL_TEXT : COL_TEXT2, 0);
        lv_obj_set_hidden(s_status[i], s_learned[i] || s_editing);
        lv_obj_set_hidden(s_tools[i], !s_editing);
    }
}

static void learn(int i)
{
    ui_learn_open_input(i, s_names[i]);
}

static void on_tile(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_editing) {
        return;
    }
    if (!s_learned[i]) {
        learn(i);
        return;
    }
    ui_action_t a = {.type = UI_ACT_INPUT, .index = i};
    ui_emit(&a);
}

static void on_learn(lv_event_t *e)
{
    learn((int)(intptr_t)lv_event_get_user_data(e));
}

static void rename_done(const char *text)
{
    int i = s_renaming;
    s_renaming = -1;
    if (i >= 0 && text[0]) {
        strlcpy(s_names[i], text, sizeof(s_names[i]));
        ui_action_t a = {.type = UI_ACT_INPUT_RENAME, .index = i, .text = text};
        ui_emit(&a);
    }
    ui_inputs_show();
}

static void rename_cancel(void)
{
    s_renaming = -1;
    ui_inputs_show();
}

static void on_rename(lv_event_t *e)
{
    s_renaming = (int)(intptr_t)lv_event_get_user_data(e);
    ui_kb_open_local(s_names[s_renaming], "Input name", UI_INPUT_NAME_MAX - 1, rename_done, rename_cancel);
}

static void on_edit(lv_event_t *e)
{
    s_editing = !s_editing;
    lv_label_set_text(s_edit_label, s_editing ? "Done" : "Edit");
    lv_obj_set_style_bg_color(s_edit_btn, s_editing ? COL_ACCENT : COL_SURFACE1, 0);
    lv_obj_set_style_text_color(s_edit_label, s_editing ? COL_ON_ACCENT : COL_TEXT2, 0);
    refresh();
}

static void on_close(lv_event_t *e)
{
    if (s_editing) {
        on_edit(e);
    }
    ui_return_to_context();
}

static lv_obj_t *small_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *b = ui_btn_text(parent, LV_SIZE_CONTENT, UI_S(40), 10, COL_SURFACE1, &font_sora_13m, COL_IR, text);
    lv_obj_set_style_pad_hor(b, UI_S(14), 0);
    lv_obj_set_style_border_width(b, UI_S(1), 0);
    lv_obj_set_style_border_color(b, COL_IR_LINE, 0);
    return b;
}

lv_obj_t *ui_inputs_create(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, UI_CENTER_W, UI_BODY_H);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(16));

    lv_obj_t *head = ui_box(p);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = ui_label(head, &font_sora_20sb, COL_TEXT, "Inputs");
    lv_obj_set_flex_grow(title, 1);
    s_edit_btn = ui_btn_text(head, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE1, &font_sora_12, COL_TEXT2, "Edit");
    lv_obj_set_style_pad_hor(s_edit_btn, UI_S(14), 0);
    lv_obj_set_style_border_width(s_edit_btn, UI_S(1), 0);
    lv_obj_set_style_border_color(s_edit_btn, COL_LINE, 0);
    s_edit_label = lv_obj_get_child(s_edit_btn, 0);
    lv_obj_add_event_cb(s_edit_btn, on_edit, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close = ui_btn_icon(head, UI_S(36), UI_S(36), 10, COL_SURFACE1, &font_icons_20, ICON_CLOSE, COL_TEXT2, NULL, false);
    lv_obj_set_style_border_width(close, UI_S(1), 0);
    lv_obj_set_style_border_color(close, COL_LINE, 0);
    lv_obj_add_event_cb(close, on_close, LV_EVENT_CLICKED, NULL);

    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *grid = ui_box(p);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_set_style_pad_row(grid, UI_GAP, 0);
    lv_obj_set_style_pad_column(grid, UI_GAP, 0);

    for (int i = 0; i < UI_INPUTS; i++) {
        lv_obj_t *t = ui_btn(grid, LV_PCT(100), LV_PCT(100), 18, COL_SURFACE2);
        lv_obj_set_grid_cell(t, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
        ui_flex(t, LV_FLEX_FLOW_COLUMN, UI_S(10));
        lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(t, UI_S(12), 0);
        lv_obj_add_event_cb(t, on_tile, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        s_name[i] = ui_label(t, &font_sora_20sb, COL_TEXT, "");
        lv_obj_set_width(s_name[i], LV_PCT(100));
        lv_obj_set_style_text_align(s_name[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_name[i], LV_LABEL_LONG_MODE_WRAP);
        s_status[i] = ui_label(t, &font_sora_12, COL_TEXT3, "Not learned");

        s_tools[i] = ui_box(t);
        lv_obj_set_size(s_tools[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        ui_flex(s_tools[i], LV_FLEX_FLOW_COLUMN, UI_S(8));
        lv_obj_set_flex_align(s_tools[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(small_btn(s_tools[i], "Rename"), on_rename, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(small_btn(s_tools[i], "Learn"), on_learn, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_snprintf(s_names[i], sizeof(s_names[i]), "Input %d", i + 1);
    }
    refresh();
    return p;
}

void ui_inputs_set(const char *const *names, const bool *learned)
{
    for (int i = 0; i < UI_INPUTS; i++) {
        if (names[i] && names[i][0]) {
            strlcpy(s_names[i], names[i], sizeof(s_names[i]));
        } else {
            lv_snprintf(s_names[i], sizeof(s_names[i]), "Input %d", i + 1);
        }
        s_learned[i] = learned[i];
    }
    refresh();
}

void ui_inputs_show(void)
{
    ui_learn_end_input();
    ui_topbar_set_target("HDMI switch");
    ui_topbar_set_ir(true);
    ui_topbar_set_context("Inputs");
    ui_show_view(VIEW_INPUTS);
}
