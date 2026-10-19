/*
 * Google TV's Home (VIEW_TV_HOME, the panel while the TV shows its home
 * screen; the Modules tab is ui_dash.c): app launcher plus quick actions.
 *
 * Edit mode shows every app. A tap hides or shows one; press and hold picks
 * it up to move it. While it is carried, the other apps make room, and
 * holding it near the top or bottom of the grid scrolls.
 */
#include <stdint.h>
#include "ui_internal.h"

#define TILE_W      ((UI_INNER_W - 2 * UI_GAP) / 3)
#define ICON        UI_APP_ICON
#define EDGE_PX     UI_S(48) /* a carried app this close to the grid's edge scrolls it */
#define SCROLL_STEP UI_S(10)

static lv_obj_t *s_grid;
static lv_obj_t *s_edit_btn;
static lv_obj_t *s_edit_label;
static bool s_editing;
static lv_obj_t *s_drag;  /* the tile being moved; it stays in the grid as a faint placeholder */
static lv_obj_t *s_ghost; /* its copy under the finger, on the top layer */

static const ui_app_info_t *tile_app(lv_obj_t *t)
{
    size_t count;
    const ui_app_info_t *apps = ui_apps_home(&count);
    size_t i = (size_t)(uintptr_t)lv_obj_get_user_data(t);
    return i < count ? &apps[i] : NULL;
}

/* The app's own icon; without one, its first letter on a tile. */
static lv_obj_t *tile_visual(lv_obj_t *parent, const ui_app_info_t *app)
{
    if (app->icon) {
        lv_obj_t *t = lv_image_create(parent);
        lv_image_set_src(t, app->icon);
        return t;
    }
    lv_obj_t *t = ui_btn(parent, ICON, ICON, 26, COL_SURFACE2);
    char mono[2] = {app->name[0] >= 'a' && app->name[0] <= 'z' ? (char)(app->name[0] & ~0x20) : app->name[0], '\0'};
    ui_label(t, &font_sora_26m, COL_ACCENT_TEXT, mono);
    return t;
}

static void refresh_async(void *arg)
{
    ui_tv_home_refresh();
}

static void tap(lv_obj_t *t)
{
    const ui_app_info_t *app = tile_app(t);
    if (!app) {
        return;
    }
    if (s_editing) {
        bool hide = !ui_apps_is_hidden(app->package);
        ui_apps_set_hidden(app->package, hide);
        lv_obj_set_style_opa(t, hide ? LV_OPA_30 : LV_OPA_COVER, 0);
        ui_action_t a = {.type = UI_ACT_APP_LAYOUT};
        ui_emit(&a);
        return;
    }
    ui_action_t a = {.type = app->on_tv ? UI_ACT_LAUNCH_APP : UI_ACT_LAUNCH,
                     .text = app->on_tv ? app->package : app->link};
    ui_emit(&a);
}

static void drag_move(void)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_set_pos(s_ghost, p.x - ICON / 2, p.y - ICON / 2);

    /* Over another app: take its place; the ones between shift along. */
    uint32_t n = lv_obj_get_child_count(s_grid);
    for (uint32_t k = 0; k < n; k++) {
        lv_obj_t *c = lv_obj_get_child(s_grid, (int32_t)k);
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        if (c != s_drag && p.x >= a.x1 && p.x <= a.x2 && p.y >= a.y1 && p.y <= a.y2) {
            lv_obj_move_to_index(s_drag, lv_obj_get_index(c));
            lv_obj_update_layout(s_grid); /* so the next move sees the new positions */
            break;
        }
    }

    lv_area_t g;
    lv_obj_get_coords(s_grid, &g);
    if (p.y < g.y1 + EDGE_PX) {
        lv_obj_scroll_by_bounded(s_grid, 0, SCROLL_STEP, LV_ANIM_OFF);
    } else if (p.y > g.y2 - EDGE_PX) {
        lv_obj_scroll_by_bounded(s_grid, 0, -SCROLL_STEP, LV_ANIM_OFF);
    }
}

static void drag_start(lv_obj_t *t)
{
    const ui_app_info_t *app = tile_app(t);
    if (!app) {
        return;
    }
    s_drag = t;
    lv_obj_set_scroll_chain(t, false); /* moving the finger now carries the app, not the grid */
    lv_obj_set_style_opa(t, LV_OPA_20, 0);
    s_ghost = tile_visual(lv_layer_top(), app);
    lv_obj_set_clickable(s_ghost, false);
    drag_move();
}

static void drag_end(void)
{
    lv_obj_delete(s_ghost);
    s_ghost = NULL;
    s_drag = NULL;
    /* Edit mode shows every app, so the grid's order is the whole order. */
    const char *packages[UI_MAX_APPS];
    size_t n = 0;
    uint32_t count = lv_obj_get_child_count(s_grid);
    for (uint32_t k = 0; k < count && n < UI_MAX_APPS; k++) {
        const ui_app_info_t *app = tile_app(lv_obj_get_child(s_grid, (int32_t)k));
        if (app) {
            packages[n++] = app->package;
        }
    }
    ui_apps_set_order(packages, n);
    ui_action_t a = {.type = UI_ACT_APP_LAYOUT};
    ui_emit(&a);
    lv_async_call(refresh_async, NULL); /* after this event: the rebuild deletes its tile */
}

static void on_tile(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_current_target_obj(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_SHORT_CLICKED:
        if (!s_drag) {
            tap(t);
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        if (s_editing && !s_drag) {
            drag_start(t);
        }
        break;
    case LV_EVENT_PRESSING:
        if (s_drag == t) {
            drag_move();
        }
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (s_drag == t) {
            drag_end();
        }
        break;
    default:
        break;
    }
}

static void app_tile(lv_obj_t *parent, const ui_app_info_t *app, size_t index)
{
    lv_obj_t *t = tile_visual(parent, app);
    lv_obj_set_clickable(t, true);
    lv_obj_set_press_lock(t, true); /* a carried app keeps its events off the tile */
    if (app->icon) {
        lv_obj_set_style_image_recolor(t, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_set_style_image_recolor_opa(t, LV_OPA_40, LV_STATE_PRESSED);
    }
    lv_obj_set_user_data(t, (void *)(uintptr_t)index);
    lv_obj_add_event_cb(t, on_tile, LV_EVENT_ALL, NULL);
    if (s_editing && ui_apps_is_hidden(app->package)) {
        lv_obj_set_style_opa(t, LV_OPA_30, 0);
    }
}

/* Sort key: the user's order first, then apps never placed, as listed. */
static long order_key(const ui_app_info_t *apps, size_t i)
{
    int rank = ui_apps_rank(apps[i].package);
    return rank >= 0 ? rank : UI_APP_LIST_MAX + (long)i;
}

void ui_tv_home_refresh(void)
{
    if (s_drag) { /* a new list arrived mid-move: drop the move */
        lv_obj_delete(s_ghost);
        s_ghost = NULL;
        s_drag = NULL;
    }
    int32_t scrolled = lv_obj_get_scroll_y(s_grid);
    lv_obj_clean(s_grid);

    size_t count;
    const ui_app_info_t *apps = ui_apps_home(&count);
    size_t order[UI_MAX_APPS];
    size_t n = 0;
    for (size_t i = 0; i < count && n < UI_MAX_APPS; i++) {
        if (s_editing || !ui_apps_is_hidden(apps[i].package)) {
            order[n++] = i;
        }
    }
    for (size_t k = 1; k < n; k++) { /* insertion sort: at most UI_MAX_APPS */
        size_t v = order[k];
        long key = order_key(apps, v);
        size_t j = k;
        for (; j > 0 && order_key(apps, order[j - 1]) > key; j--) {
            order[j] = order[j - 1];
        }
        order[j] = v;
    }
    for (size_t k = 0; k < n; k++) {
        app_tile(s_grid, &apps[order[k]], order[k]);
    }

    /* Stay where the user was, as far as the new content allows. */
    lv_obj_update_layout(s_grid);
    lv_obj_scroll_by_bounded(s_grid, 0, lv_obj_get_scroll_y(s_grid) - scrolled, LV_ANIM_OFF);
}

static void on_search(lv_event_t *e)
{
    ui_keyboard_toggle();
}

static lv_obj_t *s_inputs_btn;

static void on_inputs(lv_event_t *e)
{
    ui_inputs_show();
}

/* Inputs are an HDMI switch's learned codes. */
void ui_tv_home_ir_caps_changed(void)
{
    lv_obj_set_hidden(s_inputs_btn, !ui_ir_can_learn());
}

static void on_edit(lv_event_t *e)
{
    s_editing = !s_editing;
    lv_label_set_text(s_edit_label, s_editing ? "Done" : "Edit");
    lv_obj_set_style_bg_color(s_edit_btn, s_editing ? COL_ACCENT : COL_SURFACE1, 0);
    lv_obj_set_style_text_color(s_edit_label, s_editing ? COL_ON_ACCENT : COL_TEXT2, 0);
    ui_tv_home_refresh();
    lv_obj_scroll_to_y(s_grid, 0, LV_ANIM_OFF);
}

lv_obj_t *ui_tv_home_create(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, UI_CENTER_W, UI_BODY_H);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(16));

    lv_obj_t *head = ui_box(p);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, &font_sora_20sb, COL_TEXT, "Apps");
    s_edit_btn = ui_btn_text(head, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE1, &font_sora_12, COL_TEXT2, "Edit");
    lv_obj_set_style_pad_hor(s_edit_btn, UI_S(14), 0);
    lv_obj_set_style_border_width(s_edit_btn, UI_S(1), 0);
    lv_obj_set_style_border_color(s_edit_btn, COL_LINE, 0);
    s_edit_label = lv_obj_get_child(s_edit_btn, 0);
    lv_obj_add_event_cb(s_edit_btn, on_edit, LV_EVENT_CLICKED, NULL);

    /* Three icons per row, edge to edge like the buttons below; scrolls
     * when the TV has more apps than fit. */
    s_grid = ui_box(p);
    lv_obj_set_width(s_grid, LV_PCT(100));
    lv_obj_set_flex_grow(s_grid, 1);
    ui_flex(s_grid, LV_FLEX_FLOW_ROW_WRAP, UI_S(24));
    lv_obj_set_style_pad_column(s_grid, (UI_INNER_W - 3 * ICON) / 2, 0);
    lv_obj_set_scrollable(s_grid, true);
    lv_obj_set_clickable(s_grid, true); /* a swipe between icons scrolls too */
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_OFF);
    ui_tv_home_refresh();

    lv_obj_t *quick = ui_box(p);
    lv_obj_set_size(quick, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(quick, LV_FLEX_FLOW_ROW, UI_GAP);
    /* Search: the keyboard, which on Home types into Google TV's own search
     * (the Search key would open voice search, and there's no microphone). */
    lv_obj_t *b = ui_btn_icon(quick, TILE_W, UI_S(52), 14, COL_SURFACE2, &font_icons_20, ICON_SEARCH, COL_TEXT, "Search", false);
    lv_obj_add_event_cb(b, on_search, LV_EVENT_CLICKED, NULL);
    b = s_inputs_btn = ui_btn_icon(quick, TILE_W, UI_S(52), 14, COL_SURFACE2, &font_icons_20, ICON_INPUTS, COL_TEXT,
                                   "Inputs", false);
    lv_obj_add_event_cb(b, on_inputs, LV_EVENT_CLICKED, NULL);
    b = ui_btn_icon(quick, TILE_W, UI_S(52), 14, COL_SURFACE2, &font_icons_20, ICON_SETTINGS, COL_TEXT, "Settings", false);
    ui_btn_bind_key(b, UI_KEY_SETTINGS, UI_KEYMODE_TAP);
    return p;
}
