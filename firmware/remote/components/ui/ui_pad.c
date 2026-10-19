/*
 * D-pad (shown first) / touchpad.
 *
 * D-pad: arrows and OK, with press-and-hold sent as a real long press.
 *
 * Touchpad, a joystick around the point the finger lands on:
 * - a swipe is one move: leaving the dead zone (ZONE_PX) around that point
 *   sends one arrow, its way (the larger of the two axes);
 * - staying out there repeats it after HOLD_START_MS, faster the farther the
 *   finger is (REPEAT_SLOW_MS at the zone's edge, REPEAT_FAST_MS at FAR_PX);
 * - back in the dead zone it stops; out the other side, one arrow that way;
 * - a tap sends OK; holding LONG_OK_MS inside the dead zone is a long OK
 *   (not LVGL's long press: a finger resting before a swipe isn't one).
 */
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

#define ZONE_PX        UI_S(28)  /* the dead zone around where the finger landed */
#define ZONE_BACK_PX   UI_S(20)  /* back inside this: the zone again (no flicker at its edge) */
#define FAR_PX         UI_S(110) /* this far out, the fastest repeat */
#define HOLD_START_MS  450
#define LONG_OK_MS     600
#define REPEAT_SLOW_MS 220
#define REPEAT_FAST_MS 70
#define FLASH_MS       140
#define TILE           UI_S(22)

static lv_obj_t *s_pad_area;
static lv_obj_t *s_dpad_area;
static lv_obj_t *s_toggle[2];
static lv_obj_t *s_toggle_label[2];
static lv_obj_t *s_chev[4]; /* indexed by key - UI_KEY_DPAD_UP: up, down, left, right */
static lv_timer_t *s_flash_timer;

static lv_point_t s_origin;     /* where the finger landed */
static bool s_moved;            /* an arrow was sent: no OK on release */
static bool s_long_ok;
static int s_dir;               /* the arrow held: 0 in the dead zone */
static uint32_t s_dir_since_ms; /* when the finger left the zone that way */
static uint32_t s_last_step_ms;
static uint32_t s_press_ms;

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
    s_last_step_ms = now;
}

/* The arrow for the finger's offset from where it landed, 0 in the dead
 * zone. While an arrow is held, its axis wins ties, and the zone ends a bit
 * further in, so the edge doesn't flicker. */
static int dir_for(int32_t dx, int32_t dy, int held)
{
    int32_t ax = abs(dx), ay = abs(dy);
    int32_t out = ax > ay ? ax : ay;
    if (out < (held ? ZONE_BACK_PX : ZONE_PX)) {
        return 0;
    }
    bool held_x = held == UI_KEY_DPAD_LEFT || held == UI_KEY_DPAD_RIGHT;
    bool horizontal = held ? (held_x ? ax * 3 >= ay * 2 : ax * 2 > ay * 3) : ax >= ay;
    if (horizontal) {
        return dx > 0 ? UI_KEY_DPAD_RIGHT : UI_KEY_DPAD_LEFT;
    }
    return dy > 0 ? UI_KEY_DPAD_DOWN : UI_KEY_DPAD_UP;
}

/* The repeat interval: slower near the zone, faster further out. */
static uint32_t repeat_ms(int32_t dx, int32_t dy)
{
    int32_t out = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    float t = (float)(out - ZONE_PX) / (float)(FAR_PX - ZONE_PX);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return (uint32_t)(REPEAT_SLOW_MS - t * (REPEAT_SLOW_MS - REPEAT_FAST_MS));
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
        s_origin = p;
        s_press_ms = now;
        s_moved = false;
        s_long_ok = false;
        s_dir = 0;
        break;

    case LV_EVENT_PRESSING: {
        if (s_long_ok) {
            break; /* a long OK: moving doesn't add arrows to it */
        }
        int32_t dx = p.x - s_origin.x;
        int32_t dy = p.y - s_origin.y;
        int dir = dir_for(dx, dy, s_dir);
        if (!dir && !s_moved && now - s_press_ms >= LONG_OK_MS) {
            ui_emit_key(UI_KEY_DPAD_CENTER, UI_DIR_START_LONG);
            s_long_ok = true;
            break;
        }
        if (dir != s_dir) {
            s_dir = dir;
            s_dir_since_ms = now;
            if (dir) {
                step(dir, now); /* out of the zone, or across to another way: one arrow */
            }
        } else if (dir && now - s_dir_since_ms >= HOLD_START_MS && now - s_last_step_ms >= repeat_ms(dx, dy)) {
            step(dir, now); /* held out there: again */
        }
        break;
    }

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
