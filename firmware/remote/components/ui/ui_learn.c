/*
 * Learn flow: choose a remote and a button name (from presets), capture the
 * button from its original remote (three matching presses), then test and
 * save.
 */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

#define PRESSES 3

static const char *const KINDS[] = {"HDMI switch", "Soundbar", "Projector", "Fan", "Other"};
#define KIND_COUNT ((int)(sizeof(KINDS) / sizeof(KINDS[0])))
static const char *const LABELS[] = {
    "Power", "Input 1", "Input 2", "Input 3", "Input 4", "Up", "Down", "Left", "Right", "OK",
    "Back", "Menu", "Vol +", "Vol \xE2\x88\x92", "Mute", "Play", "Pause", "Next", "Previous", "Button",
};
#define LABEL_COUNT ((int)(sizeof(LABELS) / sizeof(LABELS[0])))

typedef struct {
    lv_obj_t *box;
    lv_obj_t *chips[UI_IR_MAX_REMOTES + KIND_COUNT + LABEL_COUNT];
    int count;
    int selected;
} chip_group_t;

static lv_obj_t *s_choose;
static lv_obj_t *s_capture;
static lv_obj_t *s_result;
static chip_group_t s_remote_chips;
static chip_group_t s_label_chips;
static lv_obj_t *s_choose_msg;
static lv_obj_t *s_capture_title;
static lv_obj_t *s_capture_status;
static lv_obj_t *s_dots[PRESSES];
static lv_obj_t *s_result_title;
static lv_obj_t *s_result_summary;

static char s_title[96];
static int s_input = -1; /* learning an Inputs page input */
static char s_input_name[24];
static char s_tv_key[12]; /* learning a key of the IR TV, or "" */

/* --- Chip groups ------------------------------------------------------------ */

static void chip_paint(chip_group_t *g)
{
    for (int i = 0; i < g->count; i++) {
        bool on = i == g->selected;
        lv_obj_set_style_bg_color(g->chips[i], on ? COL_IR : COL_SURFACE2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g->chips[i], 0), on ? COL_ON_IR : COL_TEXT, 0);
    }
}

static void on_chip(lv_event_t *e)
{
    chip_group_t *g = lv_event_get_user_data(e);
    lv_obj_t *chip = lv_event_get_current_target(e);
    for (int i = 0; i < g->count; i++) {
        if (g->chips[i] == chip) {
            g->selected = i;
        }
    }
    chip_paint(g);
}

static void chip_add(chip_group_t *g, const char *text)
{
    lv_obj_t *c = ui_btn(g->box, LV_SIZE_CONTENT, UI_S(40), 12, COL_SURFACE2);
    lv_obj_set_style_pad_hor(c, UI_S(14), 0);
    lv_obj_t *l = ui_label(c, &font_sora_13m, COL_TEXT, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(c, on_chip, LV_EVENT_CLICKED, g);
    g->chips[g->count++] = c;
}

static lv_obj_t *chip_box(lv_obj_t *parent, chip_group_t *g)
{
    g->box = ui_box(parent);
    lv_obj_set_size(g->box, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(g->box, LV_FLEX_FLOW_ROW_WRAP, UI_S(8));
    return g->box;
}

/* The learned remotes come first, as Settings lists them. */
static int remote_count(void)
{
    return (int)ui_settings_remote_count();
}

static void rebuild_remote_chips(void)
{
    lv_obj_clean(s_remote_chips.box);
    s_remote_chips.count = 0;
    for (int i = 0; i < remote_count(); i++) {
        chip_add(&s_remote_chips, ui_settings_remote_name((size_t)i));
    }
    char text[40];
    for (int k = 0; k < KIND_COUNT; k++) {
        snprintf(text, sizeof(text), "+ %s", KINDS[k]);
        /* chip_add copies the text into the label */
        chip_add(&s_remote_chips, text);
    }
    if (s_remote_chips.selected >= s_remote_chips.count) {
        s_remote_chips.selected = 0;
    }
    chip_paint(&s_remote_chips);
}

/* --- Steps -------------------------------------------------------------------- */

static void show_step(lv_obj_t *step)
{
    lv_obj_set_hidden(s_choose, step != s_choose);
    lv_obj_set_hidden(s_capture, step != s_capture);
    lv_obj_set_hidden(s_result, step != s_result);
}

static const char *selected_remote_name(void)
{
    int i = s_remote_chips.selected;
    return i < remote_count() ? ui_settings_remote_name((size_t)i) : KINDS[i - remote_count()];
}

static void start(void)
{
    if (s_tv_key[0]) {
        ui_action_t a = {.type = UI_ACT_TV_LEARN_KEY, .text = s_tv_key};
        snprintf(s_title, sizeof(s_title), "Press %s on the TV's remote", ui_irtv_key_label(s_tv_key));
        lv_label_set_text(s_capture_title, s_title);
        ui_learn_show_progress(0, PRESSES, false);
        ui_emit(&a);
        return;
    }
    if (s_input >= 0) {
        ui_action_t a = {.type = UI_ACT_INPUT_LEARN, .index = s_input};
        snprintf(s_title, sizeof(s_title), "Press %s on the HDMI switch's remote", s_input_name);
        lv_label_set_text(s_capture_title, s_title);
        ui_learn_show_progress(0, PRESSES, false);
        ui_emit(&a);
        return;
    }
    int sel = s_remote_chips.selected;
    ui_action_t a = {
        .type = UI_ACT_LEARN_START,
        .index = sel < remote_count() ? sel : -1,
        .name = selected_remote_name(),
        .text = LABELS[s_label_chips.selected],
    };
    snprintf(s_title, sizeof(s_title), "Press %s on the %s remote", LABELS[s_label_chips.selected],
             selected_remote_name());
    lv_label_set_text(s_capture_title, s_title);
    ui_learn_show_progress(0, PRESSES, false);
    ui_emit(&a);
}

static void on_start(lv_event_t *e)
{
    lv_label_set_text(s_choose_msg, "");
    start();
}

static void on_cancel(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_LEARN_CANCEL};
    ui_emit(&a);
    if (s_tv_key[0]) {
        s_tv_key[0] = '\0';
        ui_irtv_show();
        return;
    }
    if (s_input >= 0) {
        ui_inputs_show();
        return;
    }
    show_step(s_choose);
}

static void on_done(lv_event_t *e)
{
    ui_show_view(VIEW_SETTINGS);
}

static lv_obj_t *step_box(lv_obj_t *parent)
{
    lv_obj_t *s = ui_box(parent);
    lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
    ui_flex(s, LV_FLEX_FLOW_COLUMN, UI_S(12));
    return s;
}

static lv_obj_t *teal_btn(lv_obj_t *parent, const char *text, bool filled)
{
    lv_obj_t *b = ui_btn_text(parent, LV_SIZE_CONTENT, UI_S(52), 14, filled ? COL_IR : COL_SURFACE1, &font_sora_15m,
                              filled ? COL_ON_IR : COL_IR, text);
    lv_obj_set_style_pad_hor(b, UI_S(22), 0);
    if (!filled) {
        lv_obj_set_style_border_width(b, UI_S(2), 0);
        lv_obj_set_style_border_color(b, COL_IR_LINE, 0);
    }
    return b;
}

static void build_choose(lv_obj_t *panel)
{
    s_choose = step_box(panel);
    lv_obj_t *head = ui_box(s_choose);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, &font_sora_20sb, COL_TEXT, "Learn a button");
    lv_obj_t *done = ui_btn_text(head, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE1, &font_sora_12, COL_TEXT2, "Done");
    lv_obj_set_style_pad_hor(done, UI_S(14), 0);
    lv_obj_set_style_border_width(done, UI_S(1), 0);
    lv_obj_set_style_border_color(done, COL_LINE, 0);
    lv_obj_add_event_cb(done, on_done, LV_EVENT_CLICKED, NULL);

    ui_label(s_choose, &font_sora_11, COL_TEXT2, "REMOTE");
    chip_box(s_choose, &s_remote_chips);
    ui_label(s_choose, &font_sora_11, COL_TEXT2, "BUTTON");
    chip_box(s_choose, &s_label_chips);
    for (int i = 0; i < LABEL_COUNT; i++) {
        chip_add(&s_label_chips, LABELS[i]);
    }
    s_label_chips.selected = 0;
    chip_paint(&s_label_chips);

    s_choose_msg = ui_label_wrap(s_choose, &font_sora_13m, COL_ACCENT_TEXT, "");
    lv_obj_t *spacer = ui_box(s_choose);
    lv_obj_set_width(spacer, UI_S(1));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *go = teal_btn(s_choose, "Start learning", true);
    lv_obj_add_event_cb(go, on_start, LV_EVENT_CLICKED, NULL);
}

static void build_capture(lv_obj_t *panel)
{
    s_capture = step_box(panel);
    lv_obj_set_style_pad_row(s_capture, UI_S(16), 0);
    s_capture_title = ui_label_wrap(s_capture, &font_sora_20sb, COL_TEXT, "");
    ui_label_wrap(s_capture, &font_sora_15m, COL_TEXT2,
            "Point it at the IR receiver from about a hand's width away, then press and release. "
            "Three matching presses are needed.");

    lv_obj_t *row = ui_box(s_capture);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(row, UI_S(12), 0);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(28));
    for (int i = 0; i < PRESSES; i++) {
        lv_obj_t *col = ui_box(row);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        ui_flex(col, LV_FLEX_FLOW_COLUMN, UI_S(8));
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_dots[i] = ui_box(col);
        lv_obj_set_size(s_dots[i], UI_S(40), UI_S(40));
        lv_obj_set_style_radius(s_dots[i], ui_radius(LV_RADIUS_CIRCLE), 0);
        lv_obj_set_style_border_width(s_dots[i], UI_S(2), 0);
        lv_obj_set_style_border_color(s_dots[i], COL_IR, 0);
        lv_obj_set_style_bg_color(s_dots[i], COL_IR, 0);
        char text[12];
        snprintf(text, sizeof(text), "Press %d", i + 1);
        ui_label(col, &font_sora_12, COL_TEXT2, text);
    }
    s_capture_status = ui_label_wrap(s_capture, &font_sora_13m, COL_TEXT2, "");
    lv_obj_t *spacer = ui_box(s_capture);
    lv_obj_set_width(spacer, UI_S(1));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *cancel = teal_btn(s_capture, "Cancel", false);
    lv_obj_add_event_cb(cancel, on_cancel, LV_EVENT_CLICKED, NULL);
}

static void on_test(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_LEARN_TEST};
    ui_emit(&a);
}

static void on_save(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_LEARN_SAVE};
    ui_emit(&a);
}

static void on_again(lv_event_t *e)
{
    start();
}

static void build_result(lv_obj_t *panel)
{
    s_result = step_box(panel);
    lv_obj_set_style_pad_row(s_result, UI_S(16), 0);
    s_result_title = ui_label_wrap(s_result, &font_sora_20sb, COL_TEXT, "");
    s_result_summary = ui_label_wrap(s_result, &font_sora_15m, COL_TEXT2, "");
    ui_label_wrap(s_result, &font_sora_13m, COL_TEXT3,
            "Test sends it once from this remote. Point the remote at the device and check it reacts.");
    lv_obj_t *spacer = ui_box(s_result);
    lv_obj_set_width(spacer, UI_S(1));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *row = ui_box(s_result);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_GAP);
    lv_obj_add_event_cb(teal_btn(row, "Learn again", false), on_again, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(teal_btn(row, "Test", false), on_test, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(teal_btn(row, "Save", true), on_save, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *ui_learn_create(lv_obj_t *parent)
{
    lv_obj_t *panel = ui_panel(parent, UI_WIDE_W, UI_BODY_H); /* no rail: the whole width */
    build_choose(panel);
    build_capture(panel);
    build_result(panel);
    rebuild_remote_chips();
    show_step(s_choose);
    return panel;
}

/* --- API -------------------------------------------------------------------- */

void ui_learn_remotes_changed(void)
{
    rebuild_remote_chips();
}

void ui_learn_open(int remote_index)
{
    s_tv_key[0] = '\0';
    s_input = -1; /* a remote's button, not an input learned before */
    s_remote_chips.selected = (remote_index >= 0 && remote_index < remote_count()) ? remote_index : remote_count();
    chip_paint(&s_remote_chips);
    lv_label_set_text(s_choose_msg, "");
    show_step(s_choose);
}

void ui_learn_show_progress(int presses, int needed, bool mismatch)
{
    for (int i = 0; i < PRESSES; i++) {
        lv_obj_set_style_bg_opa(s_dots[i], i < presses ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (mismatch) {
        lv_obj_set_style_text_color(s_capture_status, COL_ACCENT_TEXT, 0);
        lv_label_set_text(s_capture_status, "That press didn't match the others. Press the same button again.");
    } else {
        lv_obj_set_style_text_color(s_capture_status, COL_TEXT2, 0);
        if (presses) {
            lv_label_set_text_fmt(s_capture_status, "Got %d of %d. Press it again.", presses, needed);
        } else {
            lv_label_set_text(s_capture_status, "Waiting for the first press\xE2\x80\xA6");
        }
    }
    show_step(s_capture);
}

void ui_learn_open_tv_key(const char *key)
{
    s_input = -1;
    strlcpy(s_tv_key, key ? key : "", sizeof(s_tv_key));
    ui_topbar_set_context("Learning");
    ui_show_view(VIEW_LEARN);
    start();
}

void ui_learn_open_input(int input, const char *name)
{
    s_tv_key[0] = '\0';
    s_input = input;
    strlcpy(s_input_name, name, sizeof(s_input_name));
    ui_topbar_set_context("Learning");
    ui_show_view(VIEW_LEARN);
    start();
}

void ui_learn_end_input(void)
{
    s_input = -1;
}

void ui_learn_show_result(const char *summary)
{
    lv_label_set_text_fmt(s_result_title, "Learned %s",
                          s_tv_key[0]     ? ui_irtv_key_label(s_tv_key)
                          : s_input >= 0 ? s_input_name
                                         : LABELS[s_label_chips.selected]);
    lv_label_set_text(s_result_summary, summary);
    show_step(s_result);
}

void ui_learn_show_failed(const char *reason)
{
    /* Back to where it was learning for, only if the user is still here:
     * the time-out comes even after they left. */
    bool here = ui_current_view() == VIEW_LEARN;
    if (s_tv_key[0]) {
        s_tv_key[0] = '\0';
        if (here) {
            ui_irtv_show();
            ui_toast(ICON_ALERT, "Nothing learned. Try again.");
        }
        return;
    }
    if (s_input >= 0) {
        if (here) {
            ui_inputs_show();
            ui_toast(ICON_ALERT, "Nothing learned. Try again.");
        }
        return;
    }
    lv_label_set_text(s_choose_msg, reason ? reason : "");
    show_step(s_choose);
}
