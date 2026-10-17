/*
 * Dialogs over the whole screen (ui_kit.h): a message with buttons, or a
 * list to pick from. One at a time, on lv_layer_top() above everything but
 * the toast.
 */
#include "ui_kit.h"

#define CARD_W   LV_MIN(UI_S(560), UI_W - 4 * UI_GAP)
#define CARD_H   (UI_H - 2 * UI_S(40))
#define CARD_PAD UI_S(24)
#define CARD_GAP UI_S(16)
#define TITLE_H  UI_S(28)
#define BTN_H    UI_S(48)
#define ROW_H    UI_S(52)
#define ROW_GAP  UI_S(2)
/* The tallest list that fits the card with its title and Cancel. */
#define LIST_MAX_H (CARD_H - 2 * CARD_PAD - TITLE_H - 2 * CARD_GAP - BTN_H)

static lv_obj_t *s_scrim;
static ui_modal_cb_t s_done;
static void *s_user;

/* Closes the dialog, then tells its owner. The objects go after this event
 * finishes (one of them may be running it), and done may open another. */
static void finish(int choice)
{
    if (!s_scrim) {
        return;
    }
    lv_obj_set_hidden(s_scrim, true);
    lv_obj_delete_async(s_scrim);
    s_scrim = NULL;
    ui_modal_cb_t done = s_done;
    void *user = s_user;
    s_done = NULL;
    if (done) {
        done(choice, user);
    }
}

static void on_scrim(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_scrim) {
        finish(-1); /* a tap outside the dialog */
    }
}

static void on_choice(lv_event_t *e)
{
    finish((int)(intptr_t)lv_event_get_user_data(e));
}

/* The scrim and an empty card with the title. */
static lv_obj_t *open_card(const char *title, ui_modal_cb_t done, void *user_data)
{
    ui_modal_close();
    s_done = done;
    s_user = user_data;
    s_scrim = ui_box(lv_layer_top());
    lv_obj_set_size(s_scrim, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_scrim, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_70, 0);
    lv_obj_set_clickable(s_scrim, true);
    lv_obj_add_event_cb(s_scrim, on_scrim, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = ui_box(s_scrim);
    lv_obj_set_width(card, CARD_W);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(card, CARD_H, 0);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, UI_S(1), 0);
    lv_obj_set_style_border_color(card, COL_LINE2, 0);
    lv_obj_set_style_radius(card, ui_radius(20), 0);
    lv_obj_set_style_pad_all(card, CARD_PAD, 0);
    lv_obj_set_clickable(card, true); /* taps on the card don't close it */
    ui_flex(card, LV_FLEX_FLOW_COLUMN, CARD_GAP);
    ui_label_wrap(card, &font_sora_20sb, COL_TEXT, title);
    return card;
}

/* The row of buttons at the bottom right; the last is filled. */
static void buttons_row(lv_obj_t *card, const char *const *labels, const int *choices, size_t count)
{
    lv_obj_t *row = ui_box(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (size_t i = 0; i < count; i++) {
        bool main = i + 1 == count;
        lv_obj_t *b = ui_btn_text(row, LV_SIZE_CONTENT, BTN_H, 14, main ? COL_ACCENT : COL_SURFACE3,
                                  &font_sora_15m, main ? COL_ON_ACCENT : COL_TEXT, labels[i]);
        lv_obj_set_style_pad_hor(b, UI_S(24), 0);
        lv_obj_add_event_cb(b, on_choice, LV_EVENT_CLICKED, (void *)(intptr_t)choices[i]);
    }
}

void ui_modal(const char *title, const char *message, const char *const *buttons, size_t count,
              ui_modal_cb_t done, void *user_data)
{
    lv_obj_t *card = open_card(title, done, user_data);
    if (message && message[0]) {
        ui_label_wrap(card, &font_sora_15m, COL_TEXT2, message);
    }
    int choices[3];
    count = count < 3 ? count : 3;
    for (size_t i = 0; i < count; i++) {
        choices[i] = (int)i;
    }
    buttons_row(card, buttons, choices, count);
}

void ui_modal_list(const char *title, const char *const *items, size_t count, int selected,
                   ui_modal_cb_t done, void *user_data)
{
    lv_obj_t *card = open_card(title, done, user_data);
    lv_obj_t *list = ui_box(card);
    int32_t list_h = (int32_t)count * (ROW_H + ROW_GAP) - ROW_GAP;
    lv_obj_set_size(list, LV_PCT(100), LV_MIN(list_h, LIST_MAX_H));
    ui_flex(list, LV_FLEX_FLOW_COLUMN, ROW_GAP);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    int32_t mark_y = -1; /* where the selected row starts in the list */
    for (size_t i = 0; i < count; i++) {
        bool sel = (int)i == selected;
        lv_obj_t *row = ui_btn(list, LV_PCT(100), ROW_H, 12, sel ? COL_SURFACE3 : COL_SURFACE1);
        lv_obj_set_style_pad_hor(row, UI_S(14), 0);
        ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(10));
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *l = ui_label(row, &font_sora_15m, sel ? COL_ACCENT_TEXT : COL_TEXT, items[i]);
        lv_obj_set_flex_grow(l, 1);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        if (sel) {
            ui_label(row, &font_icons_20, COL_ACCENT_TEXT, ICON_CHECK);
            mark_y = (int32_t)i * (ROW_H + ROW_GAP);
        }
        lv_obj_add_event_cb(row, on_choice, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_t *row = ui_box(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(row, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *cancel = ui_btn_text(row, LV_SIZE_CONTENT, BTN_H, 14, COL_SURFACE3, &font_sora_15m, COL_TEXT, "Cancel");
    lv_obj_set_style_pad_hor(cancel, UI_S(24), 0);
    lv_obj_add_event_cb(cancel, on_choice, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    if (mark_y >= 0 && list_h > LIST_MAX_H) {
        /* The selected row in the middle, as far as the list allows. */
        int32_t y = mark_y - (LIST_MAX_H - ROW_H) / 2;
        y = LV_CLAMP(0, y, list_h - LIST_MAX_H);
        lv_obj_update_layout(s_scrim);
        lv_obj_scroll_to_y(list, y, LV_ANIM_OFF);
    }
}

void ui_modal_close(void)
{
    finish(-1);
}

bool ui_modal_open(void)
{
    return s_scrim != NULL;
}
