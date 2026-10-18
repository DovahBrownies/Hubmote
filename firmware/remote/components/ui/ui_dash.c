/*
 * The Home tab (VIEW_DASH): one card per module that is on and has one,
 * two to a row, scrolling if there are many. And the module screen
 * (VIEW_MODULE), which holds each module's own view, built the first time
 * it is opened.
 */
#include "hub_module.h"
#include "ui_internal.h"

static lv_obj_t *s_dash;
static lv_obj_t *s_cards;
static lv_obj_t *s_card[HUB_MODULES_MAX];
static lv_obj_t *s_none;
static bool s_shown; /* cards are built the first time the tab opens */

static lv_obj_t *s_host;
static lv_obj_t *s_views[HUB_MODULES_MAX];
static const hub_module_t *s_current;

/* --- Module screen ---------------------------------------------------------- */

static int index_of(const hub_module_t *m)
{
    for (size_t i = 0; i < hub_module_count(); i++) {
        if (hub_module_get(i) == m) {
            return (int)i;
        }
    }
    return -1;
}

lv_obj_t *ui_modview_create(lv_obj_t *parent)
{
    s_host = ui_box(parent);
    lv_obj_set_size(s_host, UI_WIDE_W, UI_BODY_H);
    return s_host;
}

const hub_module_t *ui_modview_current(void)
{
    return s_current;
}

/* The module screen is going away (another view is being shown). */
void ui_modview_leave(void)
{
    if (s_current && s_current->hide) {
        s_current->hide();
    }
}

void ui_module_open(const char *id)
{
    const hub_module_t *m = hub_module_find(id);
    int i = index_of(m);
    if (i < 0 || !m->create_view || !hub_module_enabled(m)) {
        return;
    }
    if (!s_views[i]) {
        s_views[i] = ui_box(s_host);
        lv_obj_set_size(s_views[i], UI_WIDE_W, UI_BODY_H);
        m->create_view(s_views[i]);
    }
    if (ui_current_view() == VIEW_MODULE && s_current && s_current != m) {
        ui_modview_leave();
    }
    for (size_t k = 0; k < hub_module_count(); k++) {
        if (s_views[k]) {
            lv_obj_set_hidden(s_views[k], (int)k != i);
        }
    }
    bool already = ui_current_view() == VIEW_MODULE && s_current == m;
    s_current = m;
    ui_topbar_set_context(m->name);
    ui_show_view(VIEW_MODULE);
    if (!already && m->show) {
        m->show();
    }
}

/* --- Home tab --------------------------------------------------------------- */

static void on_card(lv_event_t *e)
{
    const hub_module_t *m = lv_event_get_user_data(e);
    ui_module_open(m->id);
}

lv_obj_t *ui_dash_create(lv_obj_t *parent)
{
    s_dash = ui_box(parent);
    lv_obj_set_size(s_dash, UI_WIDE_W, UI_BODY_H);
    s_cards = ui_box(s_dash);
    lv_obj_set_size(s_cards, UI_WIDE_W, UI_BODY_H);
    lv_obj_set_scrollable(s_cards, true);
    lv_obj_set_scroll_dir(s_cards, LV_DIR_VER);
    lv_obj_set_flex_flow(s_cards, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_cards, UI_GAP, 0);
    lv_obj_set_style_pad_column(s_cards, UI_GAP, 0);
    s_none = ui_label(s_cards, &font_sora_15m, COL_TEXT3, "No modules");
    lv_obj_set_style_pad_all(s_none, UI_S(24), 0);
    return s_dash;
}

/* Shows the cards of the modules that are on, making any not made yet. */
void ui_dash_refresh(void)
{
    if (!s_shown) {
        return;
    }
    size_t shown = 0;
    for (size_t i = 0; i < hub_module_count(); i++) {
        const hub_module_t *m = hub_module_get(i);
        bool on = hub_module_enabled(m) && m->create_card;
        if (on && !s_card[i]) {
            s_card[i] = m->create_card(s_cards);
            if (s_card[i]) {
                /* Two to a row, each half the width at least: a card left
                 * alone on its row takes the whole width. */
                lv_obj_set_flex_grow(s_card[i], 1);
                lv_obj_set_style_min_width(s_card[i], (UI_WIDE_W - UI_GAP) / 2, 0);
                lv_obj_set_clickable(s_card[i], true);
                lv_obj_add_event_cb(s_card[i], on_card, LV_EVENT_CLICKED, (void *)m);
            }
        }
        if (s_card[i]) {
            lv_obj_set_hidden(s_card[i], !on);
            shown += on;
        }
    }
    lv_obj_set_hidden(s_none, shown > 0);
}

void ui_dash_show(void)
{
    ui_topbar_set_context("Home");
    s_shown = true;
    ui_dash_refresh();
}
