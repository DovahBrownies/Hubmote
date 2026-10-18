/*
 * Keyboard: QWERTY letters (with a symbols layer) and a number pad.
 *
 * How it reaches the TV is picked when it opens:
 * - UI_KB_FIELD: the TV reported a focused text field. The strip mirrors
 *   the field and every edit sends the whole text (UI_ACT_TEXT), which
 *   replaces what the TV has.
 * - UI_KB_KEYS: opened by hand with no field reported. Characters go to the
 *   TV as text (UI_ACT_TYPE), Backspace and Enter as key presses; the strip
 *   only echoes what was typed. Apps such as YouTube ignore letter key
 *   presses from the remote protocol but take its text, which Android hands
 *   them as keys of its virtual keyboard.
 * - UI_KB_SEARCH: no field, on Google TV's Home. Every edit sends the whole
 *   text to Google TV's search (UI_ACT_SEARCH), which the controller runs
 *   once typing pauses, or at once on Enter.
 */
#include <string.h>
#include "ui_internal.h"

#define FIELD_H  UI_S(76)
#define GAP      UI_S(14)
#define KEYS_H   (UI_BODY_H - FIELD_H - GAP)
#define ROW_GAP  UI_S(8)
#define KEY_H    ((KEYS_H - 3 * ROW_GAP) / 4)
#define NUMPAD_W UI_S(244)
#define NUMPAD_WIDE UI_S(480) /* number fields: the pad alone, with Backspace and Enter */

/* The character on each slot key, per layer: rows of 10, 9 and 7 letter
 * keys, then the three small keys of the bottom row. */
#define SLOTS 29
static const char LETTERS[] = "qwertyuiopasdfghjklzxcvbnm,@.";
static const char SYMBOLS[] = "!@#$%^&*()-_=+[]{}|\\;:'\"/?<>~";
_Static_assert(sizeof(LETTERS) == SLOTS + 1, "one letter per slot");
_Static_assert(sizeof(SYMBOLS) == SLOTS + 1, "one symbol per slot");

static ui_kb_mode_t s_mode;
static char s_text[128];
static bool s_shift;
static bool s_symbols;
static lv_obj_t *s_title;
static lv_obj_t *s_text_label;
static lv_obj_t *s_placeholder;
static lv_obj_t *s_clear;
static lv_obj_t *s_cancel_btn; /* local mode: back without saving */
static lv_obj_t *s_shift_btn;
static lv_obj_t *s_layer_label;
static lv_obj_t *s_enter_label;
static lv_obj_t *s_slot_labels[SLOTS];
static lv_obj_t *s_letters;
static lv_obj_t *s_numpad;
static lv_obj_t *s_num_extra[2]; /* Backspace and Enter, number fields only */
static bool s_numeric;
static bool s_password;
static ui_field_t s_field;       /* the TV's focused field, applied outside local mode */
static size_t s_max_len;          /* local mode */
static ui_kb_done_cb_t s_done;
static ui_kb_cancel_cb_t s_cancel;

static const int32_t NUMPAD_COLS[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static const int32_t NUMPAD_COLS_WIDE[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                           LV_GRID_TEMPLATE_LAST};
static const int32_t NUMPAD_ROWS[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                      LV_GRID_TEMPLATE_LAST};

static char slot_char(int slot)
{
    char c = (s_symbols ? SYMBOLS : LETTERS)[slot];
    if (s_shift && c >= 'a' && c <= 'z') {
        c = (char)(c & ~0x20);
    }
    return c;
}

static void refresh_field(void)
{
    if (s_password) {
        char masked[sizeof(s_text)];
        size_t n = strlen(s_text);
        memset(masked, '*', n);
        masked[n] = '\0';
        lv_label_set_text(s_text_label, masked);
    } else {
        lv_label_set_text(s_text_label, s_text);
    }
    lv_obj_set_hidden(s_placeholder, s_text[0] != '\0');
}

static void refresh_keys(void)
{
    for (int i = 0; i < SLOTS; i++) {
        char s[2] = {slot_char(i), '\0'};
        lv_label_set_text(s_slot_labels[i], s);
    }
    lv_obj_set_style_bg_color(s_shift_btn, s_shift ? COL_ACCENT : COL_SURFACE3, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_shift_btn, 0),
                                s_shift ? COL_ON_ACCENT : (s_symbols ? COL_TEXT3 : COL_TEXT), 0);
    lv_label_set_text(s_layer_label, s_symbols ? "ABC" : "#+=");
}

/* US layout: the key that types c, and whether Shift is held for it. */
static bool key_for_char(char c, int *key, bool *shift)
{
    static const struct {
        char c;
        uint8_t key; /* Android KEYCODE_* */
        bool shift;
    } KEYS[] = {
        {' ', 62, false}, {',', 55, false}, {'.', 56, false}, {'@', 77, false},  {'-', 69, false}, {'=', 70, false},
        {'[', 71, false}, {']', 72, false}, {'\\', 73, false}, {';', 74, false}, {'\'', 75, false}, {'/', 76, false},
        {'`', 68, false}, {'+', 81, false}, {'*', 17, false},  {'#', 18, false},  {'!', 8, true},    {'$', 11, true},
        {'%', 12, true},  {'^', 13, true},  {'&', 14, true},   {'(', 16, true},   {')', 7, true},    {'_', 69, true},
        {'{', 71, true},  {'}', 72, true},  {'|', 73, true},   {':', 74, true},   {'"', 75, true},   {'<', 55, true},
        {'>', 56, true},  {'?', 76, true},  {'~', 68, true},
    };
    *shift = false;
    if (c >= 'a' && c <= 'z') {
        *key = UI_KEY_A + (c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        *key = UI_KEY_A + (c - 'A');
        *shift = true;
        return true;
    }
    if (c >= '0' && c <= '9') {
        *key = UI_KEY_0 + (c - '0');
        return true;
    }
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (KEYS[i].c == c) {
            *key = KEYS[i].key;
            *shift = KEYS[i].shift;
            return true;
        }
    }
    return false;
}

static void press_char(char c)
{
    if (c >= ' ' && c <= '~') {
        char s[2] = {c, '\0'};
        ui_action_t a = {.type = UI_ACT_TYPE, .text = s};
        ui_emit(&a);
        return;
    }
    int key;
    bool shift;
    if (!key_for_char(c, &key, &shift)) {
        return;
    }
    if (shift) {
        ui_emit_key(UI_KEY_SHIFT_LEFT, UI_DIR_START_LONG);
    }
    ui_emit_key(key, UI_DIR_SHORT);
    if (shift) {
        ui_emit_key(UI_KEY_SHIFT_LEFT, UI_DIR_END_LONG);
    }
}

static void emit_text(void)
{
    ui_action_t a = {.type = s_mode == UI_KB_SEARCH ? UI_ACT_SEARCH : UI_ACT_TEXT, .text = s_text};
    ui_emit(&a);
}

static void type_char(char c)
{
    size_t n = strlen(s_text);
    if (s_mode == UI_KB_LOCAL && n >= s_max_len) {
        return;
    }
    if (s_mode == UI_KB_KEYS) {
        press_char(c);
        if (n + 1 >= sizeof(s_text)) {
            memmove(s_text, s_text + 1, n); /* echo only: keep the latest */
            n--;
        }
    }
    if (n + 1 < sizeof(s_text)) {
        s_text[n] = c;
        s_text[n + 1] = '\0';
    }
    if (s_shift) {
        s_shift = false;
        refresh_keys();
    }
    refresh_field();
    if (s_mode == UI_KB_FIELD || s_mode == UI_KB_SEARCH) {
        emit_text();
    }
}

static void on_slot(lv_event_t *e)
{
    type_char(slot_char((int)(intptr_t)lv_event_get_user_data(e)));
}

/* Number pad and space bar: the character is the user data. */
static void on_char(lv_event_t *e)
{
    type_char((char)(intptr_t)lv_event_get_user_data(e));
}

static void on_backspace(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_LONG_PRESSED_REPEAT) {
        return;
    }
    size_t n = strlen(s_text);
    if (s_mode == UI_KB_KEYS) {
        /* Even with nothing echoed: the TV's field may already hold text. */
        ui_emit_key(UI_KEY_DEL, UI_DIR_SHORT);
        if (n) {
            s_text[n - 1] = '\0';
            refresh_field();
        }
        return;
    }
    if (n == 0) {
        return;
    }
    s_text[n - 1] = '\0';
    refresh_field();
    if (s_mode == UI_KB_FIELD || s_mode == UI_KB_SEARCH) {
        emit_text();
    }
}

static void on_shift(lv_event_t *e)
{
    if (s_symbols) {
        return;
    }
    s_shift = !s_shift;
    refresh_keys();
}

static void on_layer(lv_event_t *e)
{
    s_symbols = !s_symbols;
    s_shift = false;
    refresh_keys();
}

static void on_clear(lv_event_t *e)
{
    s_text[0] = '\0';
    refresh_field();
    if (s_mode == UI_KB_FIELD || s_mode == UI_KB_SEARCH) {
        emit_text();
    }
}

static void on_cancel_btn(lv_event_t *e)
{
    ui_kb_close();
}

static void on_enter(lv_event_t *e)
{
    if (s_mode == UI_KB_LOCAL) {
        if (s_done) {
            s_done(s_text);
        }
        return;
    }
    if (s_mode == UI_KB_FIELD) {
        ui_action_t a = {.type = UI_ACT_ENTER};
        ui_emit(&a);
        ui_return_to_context(); /* search submitted */
        return;
    }
    if (s_mode == UI_KB_SEARCH) {
        ui_action_t a = {.type = UI_ACT_SEARCH, .text = s_text, .index = 1};
        ui_emit(&a);
        ui_return_to_context(); /* the D-pad picks a result */
        return;
    }
    ui_emit_key(UI_KEY_ENTER, UI_DIR_SHORT);
    s_text[0] = '\0'; /* ready for the next field */
    refresh_field();
}

void ui_kb_close(void)
{
    if (s_mode == UI_KB_LOCAL && s_cancel) {
        s_cancel();
        return;
    }
    ui_return_to_context();
}

static void apply_field(ui_field_t field)
{
    bool numeric = field == UI_FIELD_NUMBER;
    s_password = field == UI_FIELD_PASSWORD;
    if (numeric != s_numeric) {
        s_numeric = numeric;
        lv_obj_set_hidden(s_letters, numeric);
        lv_obj_set_hidden(s_num_extra[0], !numeric);
        lv_obj_set_hidden(s_num_extra[1], !numeric);
        lv_obj_set_grid_dsc_array(s_numpad, numeric ? NUMPAD_COLS_WIDE : NUMPAD_COLS, NUMPAD_ROWS);
        lv_obj_set_width(s_numpad, numeric ? NUMPAD_WIDE : NUMPAD_W);
    }
    refresh_field();
}

static void set_mode(ui_kb_mode_t mode, const char *value, const char *label)
{
    bool field = mode == UI_KB_FIELD;
    bool local = mode == UI_KB_LOCAL;
    bool search = mode == UI_KB_SEARCH;
    s_mode = mode;
    strlcpy(s_text, ((field || local) && value) ? value : "", sizeof(s_text));
    lv_label_set_text(s_placeholder, (field && label && label[0]) ? label : "Type here");
    lv_label_set_text(s_title, local && label ? label : search ? "Search Google TV" : "Typing on the TV");
    lv_obj_set_hidden(s_title, !field && !local && !search);
    lv_label_set_text(s_enter_label, field || search ? "Search" : (local ? "Save" : "Enter"));
    lv_obj_set_hidden(s_clear, !field && !local && !search);
    lv_obj_set_hidden(s_cancel_btn, !local);
    s_shift = false;
    s_symbols = false;
    refresh_keys();
    apply_field(local ? UI_FIELD_TEXT : s_field); /* a name is never a number pad or masked */
}

static lv_obj_t *row(lv_obj_t *parent)
{
    lv_obj_t *r = ui_box(parent);
    lv_obj_set_size(r, LV_PCT(100), KEY_H);
    ui_flex(r, LV_FLEX_FLOW_ROW, ROW_GAP);
    return r;
}

static lv_obj_t *key(lv_obj_t *parent, uint8_t grow, lv_color_t bg, const lv_font_t *font, lv_color_t fg,
                     const char *text)
{
    lv_obj_t *b = ui_btn(parent, UI_S(10), KEY_H, 12, bg);
    lv_obj_set_flex_grow(b, grow);
    lv_obj_t *l = ui_label(b, font, fg, text);
    lv_obj_center(l);
    return b;
}

static void slot_keys(lv_obj_t *parent, int first, int count)
{
    for (int i = first; i < first + count; i++) {
        lv_obj_t *b = key(parent, 2, COL_SURFACE2, &font_sora_22m, COL_TEXT, "");
        s_slot_labels[i] = lv_obj_get_child(b, 0);
        lv_obj_add_event_cb(b, on_slot, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void numpad_key(lv_obj_t *grid, int col, int row_, int span, char c)
{
    lv_obj_t *b = ui_btn(grid, LV_PCT(100), LV_PCT(100), 12, COL_SURFACE2);
    lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, col, span, LV_GRID_ALIGN_STRETCH, row_, 1);
    char s[2] = {c, '\0'};
    lv_obj_center(ui_label(b, &font_sora_26m, COL_TEXT, s));
    lv_obj_add_event_cb(b, on_char, LV_EVENT_CLICKED, (void *)(intptr_t)c);
}

lv_obj_t *ui_kb_create(lv_obj_t *parent)
{
    lv_obj_t *root = ui_box(parent);
    lv_obj_set_size(root, UI_STAGE_W, UI_BODY_H);
    ui_flex(root, LV_FLEX_FLOW_COLUMN, GAP);

    /* The field: mirrored from the TV, or an echo of the keys pressed */
    lv_obj_t *field = ui_box(root);
    lv_obj_set_size(field, LV_PCT(100), FIELD_H);
    lv_obj_set_style_radius(field, ui_radius(16), 0);
    lv_obj_set_style_bg_color(field, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(field, UI_S(20), 0);
    lv_obj_set_style_pad_right(field, UI_S(12), 0);
    ui_flex(field, LV_FLEX_FLOW_ROW, UI_S(16));
    lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *col = ui_box(field);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    ui_flex(col, LV_FLEX_FLOW_COLUMN, UI_S(4));
    s_title = ui_label(col, &font_sora_11, COL_TEXT2, "Typing on the TV");
    lv_obj_t *line = ui_box(col);
    lv_obj_set_size(line, LV_PCT(100), UI_S(30));
    ui_flex(line, LV_FLEX_FLOW_ROW, UI_S(2));
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_text_label = ui_label(line, &font_sora_24m, COL_TEXT, "");
    lv_obj_set_style_max_width(s_text_label, UI_S(640), 0);
    lv_label_set_long_mode(s_text_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_t *cursor = ui_box(line);
    lv_obj_set_size(cursor, UI_S(2), UI_S(28));
    lv_obj_set_style_bg_color(cursor, COL_ACCENT_TEXT, 0);
    lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
    s_placeholder = ui_label(line, &font_sora_24m, COL_TEXT3, "Type here");
    lv_obj_set_style_pad_left(s_placeholder, UI_S(6), 0);
    s_clear = ui_btn_text(field, LV_SIZE_CONTENT, UI_S(44), 12, COL_SURFACE1, &font_sora_13m, COL_TEXT2, "Clear");
    lv_obj_set_style_pad_hor(s_clear, UI_S(16), 0);
    lv_obj_set_style_border_width(s_clear, UI_S(1), 0);
    lv_obj_set_style_border_color(s_clear, COL_LINE, 0);
    lv_obj_add_event_cb(s_clear, on_clear, LV_EVENT_CLICKED, NULL);
    s_cancel_btn = ui_btn_text(field, LV_SIZE_CONTENT, UI_S(44), 12, COL_SURFACE1, &font_sora_13m, COL_TEXT2, "Cancel");
    lv_obj_set_style_pad_hor(s_cancel_btn, UI_S(16), 0);
    lv_obj_set_style_border_width(s_cancel_btn, UI_S(1), 0);
    lv_obj_set_style_border_color(s_cancel_btn, COL_LINE, 0);
    lv_obj_add_event_cb(s_cancel_btn, on_cancel_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(s_cancel_btn, true);

    /* Keys: letters on the left, number pad on the right */
    lv_obj_t *keys = ui_box(root);
    lv_obj_set_size(keys, LV_PCT(100), KEYS_H);
    ui_flex(keys, LV_FLEX_FLOW_ROW, UI_S(20));
    lv_obj_set_flex_align(keys, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *letters = s_letters = ui_box(keys);
    lv_obj_set_height(letters, KEYS_H);
    lv_obj_set_flex_grow(letters, 1);
    ui_flex(letters, LV_FLEX_FLOW_COLUMN, ROW_GAP);

    slot_keys(row(letters), 0, 10);
    lv_obj_t *r2 = row(letters);
    lv_obj_set_style_pad_hor(r2, UI_S(27), 0);
    slot_keys(r2, 10, 9);

    lv_obj_t *r3 = row(letters);
    s_shift_btn = key(r3, 3, COL_SURFACE3, &font_icons_24, COL_TEXT, ICON_SHIFT);
    lv_obj_add_event_cb(s_shift_btn, on_shift, LV_EVENT_CLICKED, NULL);
    slot_keys(r3, 19, 7);
    lv_obj_t *bs = key(r3, 3, COL_SURFACE3, &font_icons_24, COL_TEXT, ICON_BACKSPACE);
    lv_obj_add_event_cb(bs, on_backspace, LV_EVENT_ALL, NULL);

    lv_obj_t *r4 = row(letters);
    lv_obj_t *layer = key(r4, 3, COL_SURFACE3, &font_sora_15m, COL_TEXT, "");
    s_layer_label = lv_obj_get_child(layer, 0);
    lv_obj_add_event_cb(layer, on_layer, LV_EVENT_CLICKED, NULL);
    slot_keys(r4, 26, 2);
    lv_obj_t *space = key(r4, 8, COL_SURFACE2, &font_sora_13m, COL_TEXT2, "space");
    lv_obj_add_event_cb(space, on_char, LV_EVENT_CLICKED, (void *)(intptr_t)' ');
    slot_keys(r4, 28, 1);
    lv_obj_t *enter = key(r4, 3, COL_ACCENT, &font_sora_15m, COL_ON_ACCENT, "");
    s_enter_label = lv_obj_get_child(enter, 0);
    lv_obj_add_event_cb(enter, on_enter, LV_EVENT_CLICKED, NULL);

    lv_obj_t *numpad = s_numpad = ui_box(keys);
    lv_obj_set_size(numpad, NUMPAD_W, KEYS_H);
    lv_obj_set_grid_dsc_array(numpad, NUMPAD_COLS, NUMPAD_ROWS);
    lv_obj_set_style_pad_row(numpad, ROW_GAP, 0);
    lv_obj_set_style_pad_column(numpad, ROW_GAP, 0);
    for (int d = 1; d <= 9; d++) {
        /* Calculator order: 7 8 9 on top */
        numpad_key(numpad, (d - 1) % 3, 2 - (d - 1) / 3, 1, (char)('0' + d));
    }
    numpad_key(numpad, 0, 3, 2, '0');
    numpad_key(numpad, 2, 3, 1, '.');
    lv_obj_t *nbs = s_num_extra[0] = ui_btn(numpad, LV_PCT(100), LV_PCT(100), 12, COL_SURFACE3);
    lv_obj_set_grid_cell(nbs, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 0, 2);
    lv_obj_center(ui_label(nbs, &font_icons_24, COL_TEXT, ICON_BACKSPACE));
    lv_obj_add_event_cb(nbs, on_backspace, LV_EVENT_ALL, NULL);
    lv_obj_t *nenter = s_num_extra[1] = ui_btn(numpad, LV_PCT(100), LV_PCT(100), 12, COL_ACCENT);
    lv_obj_set_grid_cell(nenter, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 2, 2);
    lv_obj_center(ui_label(nenter, &font_sora_15m, COL_ON_ACCENT, "Enter"));
    lv_obj_add_event_cb(nenter, on_enter, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(nbs, true);
    lv_obj_set_hidden(nenter, true);

    set_mode(UI_KB_KEYS, NULL, NULL);
    return root;
}

void ui_kb_open(ui_kb_mode_t mode, const char *value, const char *label)
{
    set_mode(mode, value, label);
    ui_show_view(VIEW_KEYBOARD);
}

bool ui_kb_is_local(void)
{
    return s_mode == UI_KB_LOCAL;
}

void ui_kb_follow(ui_kb_mode_t mode)
{
    if (s_mode != UI_KB_LOCAL && s_mode != mode) {
        set_mode(mode, NULL, NULL);
    }
}

/* The TV's field changed: its text goes. Not the remote's own (local)
 * typing, such as a Wi-Fi password, which the TV has nothing to do with. */
void ui_kb_clear(void)
{
    if (s_mode == UI_KB_LOCAL) {
        return;
    }
    s_text[0] = '\0';
    refresh_field();
}

void ui_kb_set_field(ui_field_t field)
{
    s_field = field;
    if (s_mode != UI_KB_LOCAL) {
        apply_field(field);
    }
}

void ui_kb_open_local(const char *value, const char *title, size_t max_len, ui_kb_done_cb_t done,
                      ui_kb_cancel_cb_t cancel)
{
    s_max_len = max_len < sizeof(s_text) - 1 ? max_len : sizeof(s_text) - 1;
    s_done = done;
    s_cancel = cancel;
    set_mode(UI_KB_LOCAL, value, title);
    ui_topbar_set_context(title);
    ui_show_view(VIEW_KEYBOARD);
}

void ui_kb_set_text(const char *value)
{
    if (s_mode != UI_KB_FIELD) {
        return;
    }
    strlcpy(s_text, value ? value : "", sizeof(s_text));
    refresh_field();
}
