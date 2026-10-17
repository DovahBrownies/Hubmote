/*
 * IR device panels: the TV over IR and learned remotes. Neither has the
 * Streamer's rail. The TV keeps the touchpad, whose arrows and OK go to the
 * TV; its VOL keys go through the Streamer (HDMI-CEC) unless Settings sends
 * volume to the TV's IR (ui_emit() decides). Keys the TV has no code for are
 * dimmed; tapping one learns it from the TV's own remote.
 */
#include <string.h>
#include "ui_internal.h"

#define GRID_COLS 4
#define GRID_ROWS 6
#define GRID_GAP  UI_S(8)

static lv_obj_t *s_mute_icon;
static lv_obj_t *s_remote_name;
static lv_obj_t *s_slots[GRID_COLS * GRID_ROWS];
static lv_obj_t *s_slot_labels[GRID_COLS * GRID_ROWS];
static lv_obj_t *s_remote_empty;
static int s_remote_index = -1;

/* Editing the learned remote: Edit, then Rename or Delete apply to the
 * selected button, or to the remote while none is. Delete takes a second
 * tap within DELETE_ARMED_MS. */
#define DELETE_ARMED_MS 4000
static lv_obj_t *s_edit_btn;
static lv_obj_t *s_edit_label;
static lv_obj_t *s_edit_bar;
static lv_obj_t *s_edit_kind;
static lv_obj_t *s_edit_name;
static lv_obj_t *s_delete_btn;
static lv_obj_t *s_delete_label;
static lv_timer_t *s_delete_timer;
static bool s_editing;
static bool s_delete_armed;
static int s_selected = -1; /* slot of the selected button, -1: the remote */
static int s_renaming = -2; /* slot being renamed, -1: the remote, -2: nothing */

static lv_obj_t *device_header(lv_obj_t *parent, const char *icon, const char *name, const char *detail,
                               lv_obj_t **name_label, lv_obj_t **detail_label)
{
    lv_obj_t *head = ui_box(parent);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *box = ui_box(head);
    lv_obj_set_size(box, UI_S(44), UI_S(44));
    lv_obj_set_style_radius(box, ui_radius(12), 0);
    lv_obj_set_style_bg_color(box, COL_SURFACE3, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_center(ui_label(box, &font_icons_24, COL_IR, icon));
    lv_obj_t *names = ui_box(head);
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(names, 1);
    ui_flex(names, LV_FLEX_FLOW_COLUMN, UI_S(3));
    lv_obj_t *n = ui_label(names, &font_sora_20sb, COL_TEXT, name);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(n, LV_PCT(100));
    if (name_label) {
        *name_label = n;
    }
    lv_obj_t *d = ui_label(names, &font_mono_11, COL_TEXT3, detail);
    lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(d, LV_PCT(100));
    if (detail_label) {
        *detail_label = d;
    }
    return head;
}

/* --- TV over IR ------------------------------------------------------------- */

typedef struct {
    const char *key;
    lv_obj_t *btn;
} tv_btn_t;

enum {
    TB_ON, TB_OFF, TB_POWER, TB_HDMI1, TB_HDMI2, TB_HDMI3, TB_HDMI4, TB_SOURCE, TB_CH_UP, TB_CH_DOWN,
    TB_BACK, TB_HOME, TB_MENU, TB_SETTINGS, TB_INFO, TB_EXIT, TB_COUNT
};

static tv_btn_t s_tv[TB_COUNT] = {
    [TB_ON] = {"power_on"},     [TB_OFF] = {"power_off"},     [TB_POWER] = {"power"}, [TB_HDMI1] = {"hdmi1"},
    [TB_HDMI2] = {"hdmi2"},     [TB_HDMI3] = {"hdmi3"},       [TB_HDMI4] = {"hdmi4"}, [TB_SOURCE] = {"source"},
    [TB_CH_UP] = {"ch_up"},     [TB_CH_DOWN] = {"ch_down"},   [TB_BACK] = {"return"}, [TB_HOME] = {"home"},
    [TB_MENU] = {"menu"},       [TB_SETTINGS] = {"settings"}, [TB_INFO] = {"info"},   [TB_EXIT] = {"exit"},
};

static char s_tv_name[32];
static char s_tv_detail[64];
static char s_tv_keys[UI_TV_KEYS_MAX][12];
static size_t s_tv_key_count;
static bool s_tv_volume_ir;
static lv_obj_t *s_tv_name_label;
static lv_obj_t *s_tv_detail_label;

bool ui_irtv_configured(void)
{
    return s_tv_name[0] != '\0';
}

const char *ui_irtv_name(void)
{
    return s_tv_name;
}

const char *ui_irtv_detail(void)
{
    return s_tv_detail;
}

bool ui_irtv_volume(void)
{
    return s_tv_volume_ir && ui_irtv_configured();
}

bool ui_irtv_has(const char *key)
{
    for (size_t i = 0; key && i < s_tv_key_count; i++) {
        if (strcmp(s_tv_keys[i], key) == 0) {
            return true;
        }
    }
    return false;
}

void ui_irtv_show(void)
{
    ui_show_ir_view(VIEW_IR_TV, ui_irtv_configured() ? s_tv_name : "TV");
}

static void on_tv_key(lv_event_t *e)
{
    const tv_btn_t *b = lv_event_get_user_data(e);
    if (ui_irtv_has(b->key)) {
        ui_action_t a = {.type = UI_ACT_IR, .text = b->key};
        ui_emit(&a);
    } else if (ui_ir_can_learn()) {
        ui_learn_open_tv_key(b->key);
    }
}

static void tv_bind(int id, lv_obj_t *btn)
{
    s_tv[id].btn = btn;
    lv_obj_add_event_cb(btn, on_tv_key, LV_EVENT_CLICKED, &s_tv[id]);
}

static lv_obj_t *tv_text_btn(lv_obj_t *parent, int id, int32_t w, int32_t h, const char *text)
{
    lv_obj_t *b = ui_btn_text(parent, w, h, 14, COL_SURFACE2, &font_sora_13m, COL_TEXT, text);
    tv_bind(id, b);
    return b;
}

/* A rounded column of three keys: CH or VOL. */
static lv_obj_t *key_column(lv_obj_t *parent)
{
    lv_obj_t *col = ui_box(parent);
    lv_obj_set_size(col, UI_S(88), LV_PCT(100));
    lv_obj_set_style_radius(col, ui_radius(16), 0);
    lv_obj_set_style_bg_color(col, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(col, UI_S(6), 0);
    ui_flex(col, LV_FLEX_FLOW_COLUMN, UI_S(4));
    return col;
}

static lv_obj_t *column_key(lv_obj_t *col, const char *icon)
{
    lv_obj_t *b = ui_btn_icon(col, LV_PCT(100), UI_S(10), 12, COL_SURFACE3, &font_icons_24, icon, COL_TEXT, NULL, false);
    lv_obj_set_flex_grow(b, 1);
    return b;
}

void ui_ir_tv_set_muted(bool muted)
{
    lv_label_set_text(s_mute_icon, muted ? ICON_MUTE : ICON_VOLUME);
    lv_obj_set_style_text_color(s_mute_icon, muted ? COL_ACCENT_TEXT : COL_TEXT2, 0);
}

static void on_tv_change(lv_event_t *e)
{
    ui_tv_setup_open();
}

static void on_tv_keys(lv_event_t *e)
{
    ui_tv_setup_open_keys();
}

static lv_obj_t *small_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *b = ui_btn_text(parent, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE2, &font_sora_12, COL_TEXT2, text);
    lv_obj_set_style_pad_hor(b, UI_S(14), 0);
    return b;
}

static void power_btn(lv_obj_t *row, int id, lv_color_t color, const char *text)
{
    lv_obj_t *b = ui_btn_icon(row, UI_S(10), UI_S(60), 14, COL_SURFACE2, &font_icons_20, ICON_POWER, color, text, false);
    lv_obj_set_flex_grow(b, 1);
    tv_bind(id, b);
}

static lv_obj_t *s_tv_keys_btn;
static lv_obj_t *s_learn_more;

void ui_ir_caps_changed(void)
{
    lv_obj_set_hidden(s_tv_keys_btn, !ui_ir_can_learn());
    lv_obj_set_hidden(s_learn_more, !ui_ir_can_learn());
}

lv_obj_t *ui_ir_tv_create(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, UI_TV_PANEL_W, UI_BODY_H);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(16));
    lv_obj_t *head = device_header(p, ICON_TV, "TV", "", &s_tv_name_label, &s_tv_detail_label);
    s_tv_keys_btn = small_btn(head, "Keys"); /* the key list is for learning missing keys */
    lv_obj_add_event_cb(s_tv_keys_btn, on_tv_keys, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(small_btn(head, "Change"), on_tv_change, LV_EVENT_CLICKED, NULL);

    /* Separate on and off codes where the TV has them: an activity can never
     * toggle the wrong way. Otherwise one Power key. */
    lv_obj_t *power = ui_box(p);
    lv_obj_set_size(power, LV_PCT(100), UI_S(60));
    ui_flex(power, LV_FLEX_FLOW_ROW, UI_GAP);
    power_btn(power, TB_ON, COL_IR, "On");
    power_btn(power, TB_OFF, COL_TEXT2, "Off");
    power_btn(power, TB_POWER, COL_IR, "Power");

    ui_label(p, &font_sora_11, COL_TEXT2, "INPUT");
    lv_obj_t *inputs = ui_box(p);
    lv_obj_set_size(inputs, LV_PCT(100), UI_S(56));
    lv_obj_set_style_margin_top(inputs, -UI_S(8), 0);
    ui_flex(inputs, LV_FLEX_FLOW_ROW, UI_S(8));
    static const char *const in_labels[] = {"HDMI 1", "HDMI 2", "HDMI 3", "HDMI 4", "Source"};
    for (int i = 0; i < 5; i++) {
        lv_obj_set_flex_grow(tv_text_btn(inputs, TB_HDMI1 + i, UI_S(10), UI_S(56), in_labels[i]), 1);
    }

    lv_obj_t *bottom = ui_box(p);
    lv_obj_set_width(bottom, LV_PCT(100));
    lv_obj_set_flex_grow(bottom, 1);
    ui_flex(bottom, LV_FLEX_FLOW_ROW, UI_GAP);

    lv_obj_t *ch = key_column(bottom);
    tv_bind(TB_CH_UP, column_key(ch, ICON_CHEVRON_UP));
    lv_obj_t *ch_label = ui_label(ch, &font_sora_11, COL_TEXT2, "CH");
    lv_obj_set_width(ch_label, LV_PCT(100));
    lv_obj_set_style_text_align(ch_label, LV_TEXT_ALIGN_CENTER, 0);
    tv_bind(TB_CH_DOWN, column_key(ch, ICON_CHEVRON_DOWN));

    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *grid = ui_box(bottom);
    lv_obj_set_height(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_set_style_pad_row(grid, UI_S(10), 0);
    lv_obj_set_style_pad_column(grid, UI_S(10), 0);
    static const char *const labels[] = {"Back", "Home", "Menu", "Settings", "Info", "Exit"};
    for (int i = 0; i < 6; i++) {
        lv_obj_t *b = tv_text_btn(grid, TB_BACK + i, LV_PCT(100), LV_PCT(100), labels[i]);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
    }

    /* Volume keys, not IR buttons: ui_emit() routes them (Streamer or TV). */
    lv_obj_t *vol = key_column(bottom);
    ui_btn_bind_key(column_key(vol, ICON_PLUS), UI_KEY_VOLUME_UP, UI_KEYMODE_HOLD);
    lv_obj_t *mute = ui_btn(vol, LV_PCT(100), UI_S(40), 12, COL_SURFACE2);
    s_mute_icon = ui_label(mute, &font_icons_20, COL_TEXT2, ICON_VOLUME);
    lv_obj_center(s_mute_icon);
    ui_btn_bind_key(mute, UI_KEY_VOLUME_MUTE, UI_KEYMODE_TAP);
    ui_btn_bind_key(column_key(vol, ICON_MINUS), UI_KEY_VOLUME_DOWN, UI_KEYMODE_HOLD);
    ui_ir_tv_update("", "", NULL, 0, false);
    return p;
}

void ui_ir_tv_update(const char *name, const char *detail, const char *const *keys, size_t count, bool volume_ir)
{
    strlcpy(s_tv_name, name ? name : "", sizeof(s_tv_name));
    strlcpy(s_tv_detail, detail ? detail : "", sizeof(s_tv_detail));
    s_tv_key_count = 0;
    for (size_t i = 0; keys && i < count && i < UI_TV_KEYS_MAX; i++) {
        strlcpy(s_tv_keys[s_tv_key_count++], keys[i], sizeof(s_tv_keys[0]));
    }
    s_tv_volume_ir = volume_ir;
    lv_label_set_text(s_tv_name_label, s_tv_name[0] ? s_tv_name : "TV");
    lv_label_set_text(s_tv_detail_label, s_tv_detail);
    for (int i = 0; i < TB_COUNT; i++) {
        lv_obj_set_style_opa(s_tv[i].btn, ui_irtv_has(s_tv[i].key) ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    /* One Power key when the TV has only a toggle; On and Off otherwise. */
    bool toggle_only = !ui_irtv_has("power_on") && !ui_irtv_has("power_off") && ui_irtv_has("power");
    lv_obj_set_hidden(s_tv[TB_ON].btn, toggle_only);
    lv_obj_set_hidden(s_tv[TB_OFF].btn, toggle_only);
    lv_obj_set_hidden(s_tv[TB_POWER].btn, !toggle_only);
}

/* --- Learned remote -------------------------------------------------------- */

static void on_learn_more(lv_event_t *e)
{
    ui_learn_open(s_remote_index);
    ui_show_view(VIEW_LEARN);
}

static void disarm_delete(void)
{
    s_delete_armed = false;
    lv_label_set_text(s_delete_label, "Delete");
    lv_obj_set_style_border_color(s_delete_btn, COL_LINE, 0);
    lv_obj_set_style_text_color(s_delete_label, COL_TEXT, 0);
    lv_timer_pause(s_delete_timer);
}

static void paint_edit(void)
{
    lv_label_set_text(s_edit_label, s_editing ? "Done" : "Edit");
    lv_obj_set_style_bg_color(s_edit_btn, s_editing ? COL_ACCENT : COL_SURFACE1, 0);
    lv_obj_set_style_text_color(s_edit_label, s_editing ? COL_ON_ACCENT : COL_TEXT2, 0);
    lv_obj_set_hidden(s_edit_bar, !s_editing);
    for (int i = 0; i < GRID_COLS * GRID_ROWS; i++) {
        lv_obj_set_style_border_width(s_slots[i], s_editing && i == s_selected ? UI_S(2) : 0, 0);
    }
    bool button = s_selected >= 0;
    lv_label_set_text(s_edit_kind, button ? "BUTTON" : "REMOTE");
    lv_label_set_text(s_edit_name, lv_label_get_text(button ? s_slot_labels[s_selected] : s_remote_name));
    disarm_delete();
}

static void set_editing(bool on)
{
    s_editing = on;
    s_selected = -1;
    paint_edit();
}

static void on_edit(lv_event_t *e)
{
    set_editing(!s_editing);
}

static void on_slot(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_editing) {
        s_selected = s_selected == slot ? -1 : slot;
        paint_edit();
        return;
    }
    ui_action_t a = {.type = UI_ACT_IR, .index = slot};
    ui_emit(&a);
}

static void rename_done(const char *text)
{
    int slot = s_renaming;
    s_renaming = -2;
    if (text[0] && slot >= -1) {
        ui_action_t a = {.type = slot >= 0 ? UI_ACT_BUTTON_RENAME : UI_ACT_REMOTE_RENAME, .index = slot, .text = text};
        ui_emit(&a); /* the remote comes back with the new name (ui_show_learned_remote) */
    }
    ui_show_ir_view(VIEW_IR_REMOTE, lv_label_get_text(s_remote_name));
}

static void rename_cancel(void)
{
    s_renaming = -2;
    ui_show_ir_view(VIEW_IR_REMOTE, lv_label_get_text(s_remote_name));
}

static void on_rename(lv_event_t *e)
{
    s_renaming = s_selected;
    if (s_selected >= 0) {
        ui_kb_open_local(lv_label_get_text(s_slot_labels[s_selected]), "Button name", UI_IR_LABEL_MAX - 1,
                         rename_done, rename_cancel);
    } else {
        ui_kb_open_local(lv_label_get_text(s_remote_name), "Remote name", UI_IR_NAME_MAX - 1, rename_done,
                         rename_cancel);
    }
}

static void on_delete_timer(lv_timer_t *t)
{
    disarm_delete();
}

static void on_delete(lv_event_t *e)
{
    if (!s_delete_armed) {
        s_delete_armed = true;
        lv_label_set_text(s_delete_label, "Confirm delete");
        lv_obj_set_style_border_color(s_delete_btn, COL_BAD, 0);
        lv_obj_set_style_text_color(s_delete_label, COL_BAD, 0);
        lv_timer_reset(s_delete_timer);
        lv_timer_resume(s_delete_timer);
        return;
    }
    disarm_delete();
    ui_action_t a = {.type = s_selected >= 0 ? UI_ACT_BUTTON_DELETE : UI_ACT_REMOTE_DELETE, .index = s_selected};
    s_selected = -1;
    if (a.type == UI_ACT_REMOTE_DELETE) {
        set_editing(false);
    }
    ui_emit(&a);
}

static lv_obj_t *edit_action(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **label)
{
    lv_obj_t *b = ui_btn_text(parent, LV_SIZE_CONTENT, UI_S(40), 10, COL_SURFACE2, &font_sora_13m, COL_TEXT, text);
    lv_obj_set_style_pad_hor(b, UI_S(14), 0);
    lv_obj_set_style_border_width(b, UI_S(1), 0);
    lv_obj_set_style_border_color(b, COL_LINE, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    if (label) {
        *label = lv_obj_get_child(b, 0);
    }
    return b;
}

lv_obj_t *ui_ir_remote_create(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, UI_CENTER_W, UI_BODY_H);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(14));
    lv_obj_t *head = device_header(p, ICON_IR, "", "Learned remote", &s_remote_name, NULL);
    s_edit_btn = ui_btn_text(head, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE1, &font_sora_12, COL_TEXT2, "Edit");
    lv_obj_set_style_pad_hor(s_edit_btn, UI_S(14), 0);
    lv_obj_set_style_border_width(s_edit_btn, UI_S(1), 0);
    lv_obj_set_style_border_color(s_edit_btn, COL_LINE, 0);
    s_edit_label = lv_obj_get_child(s_edit_btn, 0);
    lv_obj_add_event_cb(s_edit_btn, on_edit, LV_EVENT_CLICKED, NULL);

    s_edit_bar = ui_box(p);
    lv_obj_set_size(s_edit_bar, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(s_edit_bar, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(s_edit_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *what = ui_box(s_edit_bar);
    lv_obj_set_height(what, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(what, 1);
    ui_flex(what, LV_FLEX_FLOW_COLUMN, UI_S(2));
    s_edit_kind = ui_label(what, &font_mono_11, COL_TEXT3, "");
    s_edit_name = ui_label(what, &font_sora_13m, COL_TEXT, "");
    lv_label_set_long_mode(s_edit_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_edit_name, LV_PCT(100));
    edit_action(s_edit_bar, "Rename", on_rename, NULL);
    s_delete_btn = edit_action(s_edit_bar, "Delete", on_delete, &s_delete_label);
    s_delete_timer = lv_timer_create(on_delete_timer, DELETE_ARMED_MS, NULL);
    lv_timer_pause(s_delete_timer);

    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *grid = ui_box(p);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_set_style_pad_row(grid, GRID_GAP, 0);
    lv_obj_set_style_pad_column(grid, GRID_GAP, 0);
    for (int i = 0; i < GRID_COLS * GRID_ROWS; i++) {
        s_slots[i] = ui_btn(grid, LV_PCT(100), LV_PCT(100), 12, COL_SURFACE2);
        lv_obj_set_grid_cell(s_slots[i], LV_GRID_ALIGN_STRETCH, i % GRID_COLS, 1, LV_GRID_ALIGN_STRETCH,
                             i / GRID_COLS, 1);
        s_slot_labels[i] = ui_label(s_slots[i], &font_sora_13m, COL_TEXT, "");
        lv_label_set_long_mode(s_slot_labels[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(s_slot_labels[i], LV_PCT(90));
        lv_obj_set_style_text_align(s_slot_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_slot_labels[i]);
        lv_obj_set_style_border_color(s_slots[i], COL_ACCENT, 0);
        lv_obj_add_event_cb(s_slots[i], on_slot, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_hidden(s_slots[i], true);
    }
    s_remote_empty = ui_label(p, &font_sora_13m, COL_TEXT2, "No buttons yet. Learn one from its original remote.");
    lv_obj_set_width(s_remote_empty, LV_PCT(100));
    lv_label_set_long_mode(s_remote_empty, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *more = s_learn_more = ui_btn_icon(p, LV_PCT(100), UI_S(48), 12, COL_SURFACE1, &font_icons_20,
                                                ICON_ADD, COL_IR, "Learn a button", false);
    lv_obj_set_style_border_width(more, UI_S(2), 0);
    lv_obj_set_style_border_color(more, COL_IR_LINE, 0);
    lv_obj_add_event_cb(more, on_learn_more, LV_EVENT_CLICKED, NULL);
    set_editing(false);
    return p;
}

void ui_ir_remote_set(const char *name, const ui_ir_button_t *buttons, size_t count)
{
    lv_label_set_text(s_remote_name, name ? name : "");
    for (int i = 0; i < GRID_COLS * GRID_ROWS; i++) {
        lv_obj_set_hidden(s_slots[i], true);
    }
    for (size_t i = 0; i < count; i++) {
        int slot = buttons[i].slot;
        if (slot < 0 || slot >= GRID_COLS * GRID_ROWS) {
            continue;
        }
        lv_label_set_text(s_slot_labels[slot], buttons[i].label);
        lv_obj_set_hidden(s_slots[slot], false);
    }
    lv_obj_set_hidden(s_remote_empty, count != 0);
    if (s_selected >= 0 && lv_obj_is_hidden(s_slots[s_selected])) {
        s_selected = -1; /* deleted */
    }
    paint_edit();
}

int ui_ir_remote_index(void)
{
    return s_remote_index;
}

void ui_ir_remote_set_index(int index)
{
    if (index != s_remote_index && s_editing) {
        set_editing(false);
    }
    s_remote_index = index;
}

void ui_ir_remote_leave(void)
{
    if (s_editing && s_renaming == -2) {
        set_editing(false);
    }
}
