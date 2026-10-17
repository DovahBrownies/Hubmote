/*
 * D-pad (shown first) / touchpad.
 *
 * D-pad: arrows and OK, with press-and-hold sent as a real long press.
 *
 * Touchpad, meant to feel like a laptop trackpad driving a focus cursor:
 * - each swipe locks to the axis it starts on, so a slightly diagonal
 *   swipe doesn't zig-zag;
 * - the distance per arrow shrinks with speed: slow movement steps one item
 *   per STEP_SLOW_PX, a quick flick moves several (down to STEP_FAST_PX);
 * - holding still at the end of a swipe keeps repeating that arrow;
 * - a tap sends OK; a long press without moving is a long OK.
 */
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

#define STEP_SLOW_PX   UI_S(40)   /* px per arrow when moving slowly */
#define STEP_FAST_PX   UI_S(14)   /* px per arrow at FAST_PX_PER_MS and above */
#define SLOW_PX_PER_MS (0.25f * UI_H / UI_DESIGN_H)
#define FAST_PX_PER_MS (1.5f * UI_H / UI_DESIGN_H)
#define LOCK_PX        UI_S(12)   /* travel before the axis is decided */
#define PAUSE_MS       180  /* a pause this long unlocks the axis */
#define STILL_PX       UI_S(6)
#define HOLD_START_MS  450
#define HOLD_REPEAT_MS 110
#define FLASH_MS       140
#define TILE           UI_S(22)

static lv_obj_t *s_pad_area;
static lv_obj_t *s_dpad_area;
static lv_obj_t *s_toggle[2];
static lv_obj_t *s_toggle_label[2];
static lv_obj_t *s_chev[4]; /* indexed by key - UI_KEY_DPAD_UP: up, down, left, right */
static lv_timer_t *s_flash_timer;

static lv_point_t s_anchor;
static lv_point_t s_last;
static lv_point_t s_prev;      /* last sample, for the speed */
static uint32_t s_prev_ms;
static float s_speed;          /* px/ms, smoothed */
static int s_axis;             /* 0 none, 1 horizontal, 2 vertical */
static bool s_moved;
static bool s_long_ok;
static int s_last_dir;
static uint32_t s_last_step_ms;
static uint32_t s_last_motion_ms;

/* Background dot grid: one 22x22 ARGB tile, repeated. */
static uint32_t s_tile_px[TILE * TILE];
static lv_image_dsc_t s_tile = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .w = TILE,
        .h = TILE,
        .stride = TILE * 4,
    },
    .data_size = sizeof(s_tile_px),
    .data = (const uint8_t *)s_tile_px,
};

static void flash_reset(lv_timer_t *t)
{
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_text_color(s_chev[i], COL_LINE2, 0);
    }
    lv_timer_pause(t);
}

static void flash(int key)
{
    int i = key - UI_KEY_DPAD_UP;
    if (i < 0 || i > 3) {
        return;
    }
    lv_obj_set_style_text_color(s_chev[i], COL_ACCENT_TEXT, 0);
    lv_timer_reset(s_flash_timer);
    lv_timer_resume(s_flash_timer);
}

static void step(int key, uint32_t now)
{
    ui_emit_key(key, UI_DIR_SHORT);
    flash(key);
    s_moved = true;
    s_last_dir = key;
    s_last_step_ms = now;
}

static void pad_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    uint32_t now = lv_tick_get();

    switch (code) {
    case LV_EVENT_PRESSED:
        s_anchor = p;
        s_last = p;
        s_prev = p;
        s_prev_ms = now;
        s_speed = 0;
        s_axis = 0;
        s_moved = false;
        s_long_ok = false;
        s_last_dir = 0;
        s_last_motion_ms = now;
        break;

    case LV_EVENT_PRESSING: {
        if (abs(p.x - s_last.x) > STILL_PX || abs(p.y - s_last.y) > STILL_PX) {
            s_last_motion_ms = now;
            s_last = p;
        }
        if (now > s_prev_ms) {
            float inst = (float)(abs(p.x - s_prev.x) + abs(p.y - s_prev.y)) / (float)(now - s_prev_ms);
            s_speed = 0.6f * s_speed + 0.4f * inst;
            s_prev = p;
            s_prev_ms = now;
        }
        /* After a pause the next movement may pick a new axis. */
        if (s_axis && now - s_last_motion_ms > PAUSE_MS) {
            s_axis = 0;
            s_anchor = p;
        }
        int32_t dx = p.x - s_anchor.x;
        int32_t dy = p.y - s_anchor.y;
        if (!s_axis && (abs(dx) >= LOCK_PX || abs(dy) >= LOCK_PX)) {
            s_axis = abs(dx) >= abs(dy) ? 1 : 2;
        }
        float t = (s_speed - SLOW_PX_PER_MS) / (FAST_PX_PER_MS - SLOW_PX_PER_MS);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        int32_t step_px = (int32_t)(STEP_SLOW_PX - t * (STEP_SLOW_PX - STEP_FAST_PX));
        if (s_axis == 1) {
            while (abs(dx) >= step_px) {
                step(dx > 0 ? UI_KEY_DPAD_RIGHT : UI_KEY_DPAD_LEFT, now);
                s_anchor.x += dx > 0 ? step_px : -step_px;
                dx = p.x - s_anchor.x;
            }
            s_anchor.y = p.y; /* movement across the axis is ignored */
        } else if (s_axis == 2) {
            while (abs(dy) >= step_px) {
                step(dy > 0 ? UI_KEY_DPAD_DOWN : UI_KEY_DPAD_UP, now);
                s_anchor.y += dy > 0 ? step_px : -step_px;
                dy = p.y - s_anchor.y;
            }
            s_anchor.x = p.x;
        }
        /* Holding still after a swipe: keep scrolling that way. */
        if (s_moved && s_last_dir && now - s_last_motion_ms > HOLD_START_MS &&
            now - s_last_step_ms > HOLD_REPEAT_MS) {
            step(s_last_dir, now);
        }
        break;
    }

    case LV_EVENT_LONG_PRESSED:
        if (!s_moved) {
            ui_emit_key(UI_KEY_DPAD_CENTER, UI_DIR_START_LONG);
            s_long_ok = true;
        }
        break;

    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (s_long_ok) {
            ui_emit_key(UI_KEY_DPAD_CENTER, UI_DIR_END_LONG);
        } else if (!s_moved && code == LV_EVENT_RELEASED) {
            ui_emit_key(UI_KEY_DPAD_CENTER, UI_DIR_SHORT);
        }
        s_long_ok = false;
        break;

    default:
        break;
    }
}

static void set_mode(bool pad)
{
    lv_obj_set_hidden(s_pad_area, !pad);
    lv_obj_set_hidden(s_dpad_area, pad);
    for (int i = 0; i < 2; i++) {
        bool on = (i == 1) == pad; /* [0] D-pad, [1] touchpad */
        lv_obj_set_style_bg_color(s_toggle[i], on ? COL_LINE : COL_SURFACE1, 0);
        lv_obj_set_style_text_color(s_toggle_label[i], on ? COL_TEXT : COL_TEXT2, 0);
    }
}

static void on_toggle(lv_event_t *e)
{
    set_mode(lv_event_get_user_data(e) != NULL);
}

static lv_obj_t *chevron(lv_obj_t *parent, const char *icon, lv_align_t align, int32_t x, int32_t y)
{
    lv_obj_t *c = ui_label(parent, &font_icons_24, COL_LINE2, icon);
    lv_obj_align(c, align, x, y);
    return c;
}

static lv_obj_t *dpad_key(lv_obj_t *grid, const char *icon, ui_key_t key, int col, int row)
{
    lv_obj_t *b = ui_btn(grid, LV_PCT(100), LV_PCT(100), LV_RADIUS_CIRCLE, COL_SURFACE2);
    lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_t *l = ui_label(b, &font_icons_28, COL_TEXT, icon);
    lv_obj_center(l);
    ui_btn_bind_key(b, key, UI_KEYMODE_HOLD);
    return b;
}

lv_obj_t *ui_pad_create(lv_obj_t *parent)
{
    memset(s_tile_px, 0, sizeof(s_tile_px));
    /* A 2 px dot in the middle of each tile. */
    for (int y = TILE / 2 - 1; y < TILE / 2 + 1; y++) {
        for (int x = TILE / 2 - 1; x < TILE / 2 + 1; x++) {
            s_tile_px[y * TILE + x] = 0xFF2C3038;
        }
    }

    lv_obj_t *root = ui_box(parent);
    lv_obj_set_size(root, UI_PAD_W, UI_BODY_H);
    ui_flex(root, LV_FLEX_FLOW_COLUMN, UI_S(10));

    /* Touchpad | D-pad toggle */
    lv_obj_t *bar = ui_box(root);
    lv_obj_set_size(bar, LV_PCT(100), UI_S(40));
    lv_obj_set_style_bg_color(bar, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, ui_radius(12), 0);
    lv_obj_set_style_pad_all(bar, UI_S(4), 0);
    ui_flex(bar, LV_FLEX_FLOW_ROW, UI_S(4));
    static const char *const names[2] = {"D-pad", "Touchpad"};
    for (int i = 0; i < 2; i++) {
        s_toggle[i] = ui_btn(bar, UI_S(10), LV_PCT(100), 9, COL_SURFACE1);
        lv_obj_set_flex_grow(s_toggle[i], 1);
        s_toggle_label[i] = ui_label(s_toggle[i], &font_sora_12, COL_TEXT2, names[i]);
        lv_obj_center(s_toggle_label[i]);
        lv_obj_add_event_cb(s_toggle[i], on_toggle, LV_EVENT_CLICKED, i == 1 ? (void *)1 : NULL);
    }

    /* Touchpad */
    s_pad_area = ui_box(root);
    lv_obj_set_width(s_pad_area, LV_PCT(100));
    lv_obj_set_flex_grow(s_pad_area, 1);
    lv_obj_set_clickable(s_pad_area, true);
    lv_obj_set_press_lock(s_pad_area, true);
    lv_obj_set_style_radius(s_pad_area, ui_radius(24), 0);
    lv_obj_set_style_bg_color(s_pad_area, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(s_pad_area, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(s_pad_area, &s_tile, 0);
    lv_obj_set_style_bg_image_tiled(s_pad_area, true, 0);
    lv_obj_add_event_cb(s_pad_area, pad_event_cb, LV_EVENT_ALL, NULL);

    s_chev[0] = chevron(s_pad_area, ICON_CHEVRON_UP, LV_ALIGN_TOP_MID, 0, UI_S(18));
    s_chev[1] = chevron(s_pad_area, ICON_CHEVRON_DOWN, LV_ALIGN_BOTTOM_MID, 0, -UI_S(18));
    s_chev[2] = chevron(s_pad_area, ICON_CHEVRON_LEFT, LV_ALIGN_LEFT_MID, UI_S(18), 0);
    s_chev[3] = chevron(s_pad_area, ICON_CHEVRON_RIGHT, LV_ALIGN_RIGHT_MID, -UI_S(18), 0);

    lv_obj_t *ring = ui_box(s_pad_area);
    lv_obj_set_size(ring, UI_S(76), UI_S(76));
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, ui_radius(LV_RADIUS_CIRCLE), 0);
    lv_obj_set_style_border_width(ring, UI_S(2), 0);
    lv_obj_set_style_border_color(ring, COL_LINE2, 0);
    lv_obj_t *dot = ui_box(ring);
    lv_obj_set_size(dot, UI_S(10), UI_S(10));
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, ui_radius(LV_RADIUS_CIRCLE), 0);
    lv_obj_set_style_bg_color(dot, COL_ACCENT_TEXT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    /* D-pad */
    s_dpad_area = ui_box(root);
    lv_obj_set_width(s_dpad_area, LV_PCT(100));
    lv_obj_set_flex_grow(s_dpad_area, 1);
    lv_obj_set_style_radius(s_dpad_area, ui_radius(24), 0);
    lv_obj_set_style_bg_color(s_dpad_area, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(s_dpad_area, LV_OPA_COVER, 0);

    static const int32_t cols[] = {UI_S(96), UI_S(120), UI_S(96), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {UI_S(96), UI_S(120), UI_S(96), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *disc = ui_box(s_dpad_area);
    lv_obj_set_size(disc, UI_S(312), UI_S(312));
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, ui_radius(LV_RADIUS_CIRCLE), 0);
    lv_obj_set_style_bg_color(disc, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_layout(disc, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(disc, cols, rows);
    dpad_key(disc, ICON_CHEVRON_UP, UI_KEY_DPAD_UP, 1, 0);
    dpad_key(disc, ICON_CHEVRON_LEFT, UI_KEY_DPAD_LEFT, 0, 1);
    dpad_key(disc, ICON_CHEVRON_RIGHT, UI_KEY_DPAD_RIGHT, 2, 1);
    dpad_key(disc, ICON_CHEVRON_DOWN, UI_KEY_DPAD_DOWN, 1, 2);
    lv_obj_t *ok = ui_btn(disc, LV_PCT(100), LV_PCT(100), LV_RADIUS_CIRCLE, COL_SURFACE3);
    lv_obj_set_grid_cell(ok, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_style_border_width(ok, UI_S(2), 0);
    lv_obj_set_style_border_color(ok, COL_ACCENT_TEXT, 0);
    lv_obj_t *ok_label = ui_label(ok, &font_sora_16sb, COL_TEXT, "OK");
    lv_obj_center(ok_label);
    ui_btn_bind_key(ok, UI_KEY_DPAD_CENTER, UI_KEYMODE_HOLD);

    s_flash_timer = lv_timer_create(flash_reset, FLASH_MS, NULL);
    lv_timer_pause(s_flash_timer);
    set_mode(false); /* D-pad first */
    return root;
}
