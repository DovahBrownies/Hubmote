/* Fixed rail: never moves, so it can be used without looking. */
#include "ui_internal.h"

static lv_obj_t *s_mute_icon;
static lv_obj_t *s_kb_btn;
static lv_obj_t *s_kb_icon;
static lv_obj_t *s_kb_caption;

static void on_keyboard(lv_event_t *e)
{
    ui_keyboard_toggle();
}

static lv_obj_t *rail_key(lv_obj_t *parent, int32_t h, const char *icon, const char *caption, ui_key_t key)
{
    lv_obj_t *b = ui_btn_icon(parent, UI_RAIL_W, h, 18, COL_SURFACE2, &font_icons_24, icon, COL_TEXT, caption, true);
    ui_btn_bind_key(b, key, UI_KEYMODE_TAP);
    return b;
}

lv_obj_t *ui_rail_create(lv_obj_t *parent)
{
    lv_obj_t *rail = ui_box(parent);
    lv_obj_set_size(rail, UI_RAIL_W, UI_BODY_H);
    ui_flex(rail, LV_FLEX_FLOW_COLUMN, UI_S(10));

    lv_obj_t *power = ui_btn_icon(rail, UI_RAIL_W, UI_S(48), 14, COL_SURFACE1, &font_icons_20, ICON_POWER, COL_TEXT2, NULL, false);
    ui_btn_bind_key(power, UI_KEY_POWER, UI_KEYMODE_TAP);

    rail_key(rail, UI_S(76), ICON_BACK, "Back", UI_KEY_BACK);
    rail_key(rail, UI_S(76), ICON_HOME, "Home", UI_KEY_HOME);
    /* Play lives in the app panel; the rail's third key opens the keyboard. */
    s_kb_btn = ui_btn_icon(rail, UI_RAIL_W, UI_S(76), 18, COL_SURFACE2, &font_icons_24, ICON_KEYBOARD, COL_TEXT, "Keyboard",
                           true);
    s_kb_icon = lv_obj_get_child(s_kb_btn, 0);
    s_kb_caption = lv_obj_get_child(s_kb_btn, 1);
    lv_obj_add_event_cb(s_kb_btn, on_keyboard, LV_EVENT_CLICKED, NULL);

    lv_obj_t *vol = ui_box(rail);
    lv_obj_set_width(vol, UI_RAIL_W);
    lv_obj_set_flex_grow(vol, 1);
    lv_obj_set_style_bg_color(vol, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(vol, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(vol, ui_radius(20), 0);
    lv_obj_set_style_pad_all(vol, UI_S(6), 0);
    ui_flex(vol, LV_FLEX_FLOW_COLUMN, UI_S(6));

    lv_obj_t *up = ui_btn_icon(vol, LV_PCT(100), UI_S(10), 14, COL_SURFACE2, &font_icons_28, ICON_PLUS, COL_TEXT, NULL, false);
    lv_obj_set_flex_grow(up, 1);
    /* Held volume is a real key hold (down ... up): the Streamer passes it on
     * over HDMI-CEC as pressed/released and the TV stops on release. A stream
     * of taps (~10/s) outran CEC and kept changing the volume afterwards. */
    ui_btn_bind_key(up, UI_KEY_VOLUME_UP, UI_KEYMODE_HOLD);

    lv_obj_t *mute = ui_btn(vol, LV_PCT(100), UI_S(48), 14, COL_SURFACE1);
    s_mute_icon = ui_label(mute, &font_icons_24, COL_TEXT2, ICON_VOLUME);
    lv_obj_center(s_mute_icon);
    ui_btn_bind_key(mute, UI_KEY_VOLUME_MUTE, UI_KEYMODE_TAP);

    lv_obj_t *down = ui_btn_icon(vol, LV_PCT(100), UI_S(10), 14, COL_SURFACE2, &font_icons_28, ICON_MINUS, COL_TEXT, NULL, false);
    lv_obj_set_flex_grow(down, 1);
    ui_btn_bind_key(down, UI_KEY_VOLUME_DOWN, UI_KEYMODE_HOLD);
    return rail;
}

void ui_rail_set_kb(bool open)
{
    lv_obj_set_style_bg_color(s_kb_btn, open ? COL_ACCENT : COL_SURFACE2, 0);
    lv_obj_set_style_text_color(s_kb_icon, open ? COL_ON_ACCENT : COL_TEXT, 0);
    lv_obj_set_style_text_color(s_kb_caption, open ? COL_ON_ACCENT : COL_TEXT, 0);
}

void ui_rail_set_volume(int level, int max, bool muted)
{
    (void)level;
    (void)max;
    lv_label_set_text(s_mute_icon, muted ? ICON_MUTE : ICON_VOLUME);
    lv_obj_set_style_text_color(s_mute_icon, muted ? COL_ACCENT_TEXT : COL_TEXT2, 0);
}
