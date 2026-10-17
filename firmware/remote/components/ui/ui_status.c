/* Loading, error and empty states (ui_kit.h): one look for every screen. */
#include "ui_internal.h"

#define STATES 4

typedef struct {
    bool compact;
    lv_obj_t *spinner, *icon, *title, *detail, *actions;
    struct {
        const lv_font_t *font;
        const char *icon;
        lv_color_t color;
    } icons[STATES];
} status_t;

static status_t *get(lv_obj_t *obj)
{
    return lv_obj_get_user_data(obj);
}

static void on_delete(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_status_create(lv_obj_t *parent, int32_t w, int32_t h, bool compact)
{
    status_t *st = lv_zalloc(sizeof(*st));
    LV_ASSERT_MALLOC(st);
    st->compact = compact;
    st->icons[UI_STATUS_ERROR].font = compact ? &font_icons_24 : &font_icons_28;
    st->icons[UI_STATUS_ERROR].icon = ICON_ALERT;
    st->icons[UI_STATUS_ERROR].color = COL_ACCENT_TEXT;

    lv_obj_t *obj = compact ? ui_box(parent) : ui_panel(parent, w, h);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_user_data(obj, st);
    lv_obj_add_event_cb(obj, on_delete, LV_EVENT_DELETE, st);

    lv_obj_t *text;
    if (compact) {
        ui_flex(obj, LV_FLEX_FLOW_ROW, UI_S(16));
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        st->spinner = ui_spinner(obj, UI_S(40), COL_ACCENT_TEXT);
        st->icon = ui_label(obj, &font_icons_24, COL_TEXT2, "");
        text = ui_box(obj);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_height(text, LV_SIZE_CONTENT);
        ui_flex(text, LV_FLEX_FLOW_COLUMN, UI_S(4));
        st->title = ui_label(text, &font_sora_16sb, COL_TEXT, "");
        st->detail = ui_label(text, &font_sora_13m, COL_TEXT2, "");
    } else {
        ui_flex(obj, LV_FLEX_FLOW_COLUMN, UI_S(14));
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        st->spinner = ui_spinner(obj, UI_S(64), COL_ACCENT_TEXT);
        st->icon = ui_label(obj, &font_icons_28, COL_TEXT2, "");
        text = obj;
        st->title = ui_label(obj, &font_sora_20sb, COL_TEXT, "");
        st->detail = ui_label(obj, &font_sora_15m, COL_TEXT2, "");
    }
    lv_obj_t *labels[] = {st->title, st->detail};
    for (int i = 0; i < 2; i++) {
        lv_label_set_long_mode(labels[i], compact ? LV_LABEL_LONG_MODE_DOTS : LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(labels[i], compact ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        if (compact) {
            lv_obj_set_width(labels[i], LV_PCT(100));
        } else {
            lv_obj_set_style_max_width(labels[i], UI_S(560), 0); /* wraps beyond this */
        }
    }
    st->actions = ui_box(text);
    lv_obj_set_size(st->actions, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(st->actions, LV_FLEX_FLOW_ROW, UI_S(12));
    if (!compact) {
        lv_obj_set_style_margin_top(st->actions, UI_S(8), 0);
    }
    ui_status_set(obj, UI_STATUS_NONE, NULL, NULL);
    return obj;
}

static void set_text(lv_obj_t *label, const char *text)
{
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_hidden(label, !text || !text[0]);
}

void ui_status_set(lv_obj_t *obj, ui_status_t state, const char *title, const char *detail)
{
    status_t *st = get(obj);
    lv_obj_set_hidden(obj, state == UI_STATUS_NONE);
    ui_spinner_run(st->spinner, state == UI_STATUS_LOADING);
    if (state == UI_STATUS_NONE) {
        return;
    }
    const char *icon = st->icons[state].icon;
    lv_obj_set_hidden(st->icon, !icon);
    if (icon) {
        lv_obj_set_style_text_font(st->icon, st->icons[state].font, 0);
        lv_obj_set_style_text_color(st->icon, st->icons[state].color, 0);
        lv_label_set_text(st->icon, icon);
    }
    set_text(st->title, title);
    set_text(st->detail, detail);

    bool any = false;
    uint32_t n = lv_obj_get_child_count(st->actions);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *b = lv_obj_get_child(st->actions, (int32_t)i);
        bool on = (ui_status_t)(uintptr_t)lv_obj_get_user_data(b) == state;
        lv_obj_set_hidden(b, !on);
        any |= on;
    }
    lv_obj_set_hidden(st->actions, !any);
}

void ui_status_set_icon(lv_obj_t *obj, ui_status_t state, const lv_font_t *font, const char *icon,
                        lv_color_t color)
{
    status_t *st = get(obj);
    if (state == UI_STATUS_ERROR || state == UI_STATUS_EMPTY) {
        st->icons[state].font = font;
        st->icons[state].icon = icon;
        st->icons[state].color = color;
    }
}

lv_obj_t *ui_status_add_action(lv_obj_t *obj, ui_status_t state, const char *icon, const char *text,
                               bool primary, lv_event_cb_t cb, void *user_data)
{
    status_t *st = get(obj);
    lv_color_t bg = primary ? COL_ACCENT : COL_SURFACE2;
    lv_color_t fg = primary ? COL_ON_ACCENT : COL_TEXT;
    lv_obj_t *b = ui_btn(st->actions, LV_SIZE_CONTENT, st->compact ? UI_S(40) : UI_S(52), 14, bg);
    ui_flex(b, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(b, UI_S(20), 0);
    if (icon) {
        ui_label(b, &font_icons_20, fg, icon);
    }
    ui_label(b, &font_sora_13m, fg, text);
    lv_obj_set_user_data(b, (void *)(uintptr_t)state);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_set_hidden(b, true);
    return b;
}
