/* The building blocks of ui_kit.h and ui_internal.h: plain containers,
 * cards, buttons, key bindings. */
#include <stdint.h>
#include "ui_internal.h"

#define KEY_MASK      0xFFFFu
#define MODE_SHIFT    16
#define HOLDING_FLAG  (1u << 24)

typedef struct {
    ui_action_type_t type;
    const char *text;
    int index;
} action_binding_t;

lv_obj_t *ui_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

void ui_flex(lv_obj_t *obj, lv_flex_flow_t flow, int32_t gap)
{
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_style_pad_row(obj, gap, 0);
    lv_obj_set_style_pad_column(obj, gap, 0);
}

lv_obj_t *ui_panel(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *p = ui_box(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, ui_radius(20), 0);
    lv_obj_set_style_pad_all(p, UI_S(20), 0);
    return p;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *ui_label_wrap(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = ui_label(parent, font, color, text);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

lv_obj_t *ui_btn(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius, lv_color_t bg)
{
    lv_obj_t *b = ui_box(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_clickable(b, true);
    lv_obj_set_style_radius(b, ui_radius(radius), 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, ui_pressed(bg), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    /* Children are decoration; the button takes the touch. */
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return b;
}

lv_obj_t *ui_btn_icon(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius, lv_color_t bg,
                      const lv_font_t *icon_font, const char *icon, lv_color_t fg, const char *caption,
                      bool vertical)
{
    lv_obj_t *b = ui_btn(parent, w, h, radius, bg);
    ui_flex(b, vertical ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW, vertical ? UI_S(6) : UI_S(8));
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(b, icon_font, fg, icon);
    if (caption) {
        ui_label(b, vertical ? &font_sora_11 : &font_sora_13m, fg, caption);
    }
    return b;
}

lv_obj_t *ui_btn_text(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius, lv_color_t bg,
                      const lv_font_t *font, lv_color_t fg, const char *text)
{
    lv_obj_t *b = ui_btn(parent, w, h, radius, bg);
    lv_obj_t *l = ui_label(b, font, fg, text);
    lv_obj_center(l);
    return b;
}

#define SPIN_MS  1000
#define SPIN_ARC 200

lv_obj_t *ui_spinner(lv_obj_t *parent, int32_t size, lv_color_t color)
{
    lv_obj_t *s = lv_spinner_create(parent);
    int32_t w = size >= UI_S(40) ? UI_S(5) : size >= UI_S(24) ? UI_S(4) : UI_S(2);
    lv_obj_set_size(s, size, size);
    lv_obj_set_clickable(s, false);
    lv_obj_set_style_arc_color(s, COL_SURFACE3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s, w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s, w, LV_PART_INDICATOR);
    lv_spinner_set_anim_params(s, SPIN_MS, SPIN_ARC);
    return s;
}

void ui_spinner_run(lv_obj_t *spinner, bool run)
{
    if (run != lv_obj_is_hidden(spinner)) {
        return; /* already in that state: don't restart the animation */
    }
    lv_obj_set_hidden(spinner, !run);
    if (run) {
        lv_spinner_set_anim_params(spinner, SPIN_MS, SPIN_ARC);
    } else {
        lv_anim_delete(spinner, NULL);
    }
}

static void key_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    uint32_t data = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
    int key = (int)(data & KEY_MASK);
    ui_keymode_t mode = (ui_keymode_t)((data >> MODE_SHIFT) & 0xFF);
    lv_event_code_t code = lv_event_get_code(e);

    switch (mode) {
    case UI_KEYMODE_TAP:
        if (code == LV_EVENT_CLICKED) {
            ui_emit_key(key, UI_DIR_SHORT);
        }
        break;
    case UI_KEYMODE_REPEAT:
        if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED ||
            code == LV_EVENT_LONG_PRESSED_REPEAT) {
            ui_emit_key(key, UI_DIR_SHORT);
        }
        break;
    case UI_KEYMODE_HOLD:
        if (code == LV_EVENT_SHORT_CLICKED) {
            ui_emit_key(key, UI_DIR_SHORT);
        } else if (code == LV_EVENT_LONG_PRESSED) {
            ui_emit_key(key, UI_DIR_START_LONG);
            data |= HOLDING_FLAG;
        } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && (data & HOLDING_FLAG)) {
            ui_emit_key(key, UI_DIR_END_LONG);
            data &= ~HOLDING_FLAG;
        }
        lv_obj_set_user_data(btn, (void *)(uintptr_t)data);
        break;
    }
}

lv_obj_t *ui_badge(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *dot = ui_box(parent);
    lv_obj_set_floating(dot, true); /* out of the parent's flex layout */
    lv_obj_set_size(dot, UI_S(10), UI_S(10));
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, x, y);
    lv_obj_set_hidden(dot, true);
    return dot;
}

void ui_btn_bind_key(lv_obj_t *btn, ui_key_t key, ui_keymode_t mode)
{
    uint32_t data = ((uint32_t)key & KEY_MASK) | ((uint32_t)mode << MODE_SHIFT);
    lv_obj_set_user_data(btn, (void *)(uintptr_t)data);
    lv_obj_add_event_cb(btn, key_event_cb, LV_EVENT_ALL, NULL);
}

static void action_event_cb(lv_event_t *e)
{
    action_binding_t *b = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        lv_free(b); /* lists rebuilt on each scan would leak one per row */
        return;
    }
    ui_action_t a = {.type = b->type, .text = b->text, .index = b->index};
    ui_emit(&a);
}

void ui_btn_bind_action(lv_obj_t *btn, ui_action_type_t type, const char *text, int index)
{
    action_binding_t *b = lv_malloc(sizeof(*b));
    if (!b) {
        return;
    }
    b->type = type;
    b->text = text;
    b->index = index;
    lv_obj_add_event_cb(btn, action_event_cb, LV_EVENT_CLICKED, b);
    lv_obj_add_event_cb(btn, action_event_cb, LV_EVENT_DELETE, b);
}
