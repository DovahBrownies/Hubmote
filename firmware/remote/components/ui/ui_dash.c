/*
 * The Home tab (VIEW_DASH): a card per module that is on and has one, on a
 * grid of four columns (HUB_CARD_*: small cards take one, medium two,
 * large all four), scrolling if there are many. The user arranges it: a
 * long press on a card, or Settings > Modules > Arrange Home, puts arrows
 * on each card to move it and a button to change its size. The order also
 * sets the switcher's and which module's glance the top bar shows; it is
 * saved as ui_home_layout() gives it.
 *
 * And the module screen (VIEW_MODULE), which holds each module's own view,
 * built the first time it is opened.
 */
#include <stdio.h>
#include <string.h>
#include "hub_module.h"
#include "ui_internal.h"

/* --- Module screen ---------------------------------------------------------- */

static lv_obj_t *s_host;
static lv_obj_t *s_views[HUB_MODULES_MAX];
static const hub_module_t *s_current;

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

/* --- The layout: the modules in order, with their cards' sizes -------------- */

typedef struct {
    const hub_module_t *m;
    hub_card_size_t size;
    lv_obj_t *card;        /* NULL until the Home tab shows it */
    hub_card_size_t built; /* the size card was made at */
    lv_obj_t *cover;       /* over the card while arranging */
    lv_obj_t *prev, *next; /* its arrows */
    lv_obj_t *size_btn;
} slot_t;

static slot_t s_slot[HUB_MODULES_MAX];
static size_t s_slots; /* 0 until built from s_layout and the modules */
static char s_layout[UI_HOME_LAYOUT_MAX];

static unsigned sizes_of(const hub_module_t *m)
{
    return m->card_sizes ? m->card_sizes : HUB_CARD_MEDIUM;
}

/* size if the module offers it, else its default, else its first. */
static hub_card_size_t fit_size(const hub_module_t *m, unsigned size)
{
    unsigned offered = sizes_of(m);
    if (size & offered) {
        return (hub_card_size_t)size;
    }
    unsigned def = m->card_size ? m->card_size : HUB_CARD_MEDIUM;
    if (def & offered) {
        return (hub_card_size_t)def;
    }
    for (unsigned s = HUB_CARD_SMALL; s <= HUB_CARD_LARGE; s <<= 1) {
        if (s & offered) {
            return (hub_card_size_t)s;
        }
    }
    return HUB_CARD_MEDIUM;
}

static char size_letter(hub_card_size_t size)
{
    return size == HUB_CARD_SMALL ? 'S' : size == HUB_CARD_LARGE ? 'L' : 'M';
}

static int slot_of(const hub_module_t *m)
{
    for (size_t i = 0; i < s_slots; i++) {
        if (s_slot[i].m == m) {
            return (int)i;
        }
    }
    return -1;
}

/* Orders the slots as s_layout says ("weather:L,sleep_timer:M"); modules it
 * doesn't name follow in name order, at their default size. Cards already
 * made stay with their module. */
static void build_slots(void)
{
    static slot_t old[HUB_MODULES_MAX];
    size_t old_count = s_slots;
    memcpy(old, s_slot, sizeof(slot_t) * old_count);
    memset(s_slot, 0, sizeof(s_slot));
    s_slots = 0;

    const char *p = s_layout;
    while (*p && s_slots < HUB_MODULES_MAX) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char item[HUB_MODULE_ID_MAX + 4];
        size_t n = len < sizeof(item) - 1 ? len : sizeof(item) - 1;
        memcpy(item, p, n);
        item[n] = '\0';
        unsigned size = 0;
        char *colon = strchr(item, ':');
        if (colon) {
            *colon = '\0';
            size = colon[1] == 'S' ? HUB_CARD_SMALL : colon[1] == 'L' ? HUB_CARD_LARGE : HUB_CARD_MEDIUM;
        }
        const hub_module_t *m = hub_module_find(item);
        if (m && slot_of(m) < 0) {
            s_slot[s_slots++] = (slot_t){.m = m, .size = fit_size(m, size)};
        }
        p += len;
        if (*p == ',') {
            p++;
        }
    }
    for (size_t i = 0; i < hub_module_count() && s_slots < HUB_MODULES_MAX; i++) {
        const hub_module_t *m = hub_module_get(i);
        if (slot_of(m) < 0) {
            s_slot[s_slots++] = (slot_t){.m = m, .size = fit_size(m, 0)};
        }
    }
    for (size_t i = 0; i < old_count; i++) {
        int k = slot_of(old[i].m);
        if (k >= 0) {
            hub_card_size_t size = s_slot[k].size;
            s_slot[k] = old[i];
            s_slot[k].size = size; /* a new size makes the card again */
        } else if (old[i].card) {
            lv_obj_delete(old[i].card);
        }
    }
}

static void ensure_slots(void)
{
    if (s_slots != hub_module_count()) {
        build_slots();
    }
}

static void layout_text(char *out, size_t len)
{
    size_t at = 0;
    out[0] = '\0';
    for (size_t i = 0; i < s_slots && at < len; i++) {
        at += snprintf(out + at, len - at, "%s%s:%c", i ? "," : "", s_slot[i].m->id, size_letter(s_slot[i].size));
    }
}

size_t ui_home_module_count(void)
{
    ensure_slots();
    return s_slots;
}

const hub_module_t *ui_home_module(size_t index)
{
    ensure_slots();
    return index < s_slots ? s_slot[index].m : NULL;
}

/* --- Home tab --------------------------------------------------------------- */

static lv_obj_t *s_dash;
static lv_obj_t *s_cards;
static lv_obj_t *s_none;
static lv_obj_t *s_done;
static bool s_shown; /* cards are made the first time the tab opens */
static bool s_arranging;

static bool shown_on_home(const slot_t *sl)
{
    return hub_module_enabled(sl->m) && sl->m->create_card;
}

static const char *size_name(hub_card_size_t size)
{
    return size == HUB_CARD_SMALL ? "Small" : size == HUB_CARD_LARGE ? "Large" : "Medium";
}

/* The layout changed: show it, save it, and let the glance follow. */
static void changed(void)
{
    ui_dash_refresh();
    ui_topbar_refresh_glance();
    layout_text(s_layout, sizeof(s_layout));
    ui_action_t a = {.type = UI_ACT_HOME_LAYOUT};
    ui_emit(&a);
}

static void on_card(lv_event_t *e)
{
    const hub_module_t *m = lv_event_get_user_data(e);
    if (!s_arranging) {
        ui_module_open(m->id);
    }
}

static void on_card_long(lv_event_t *e)
{
    ui_dash_arrange(true);
}

/* Moves a card past the next one shown in that direction (-1 or 1). */
static void on_move(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int by = (int)(intptr_t)lv_obj_get_user_data(btn);
    int i = slot_of(lv_event_get_user_data(e));
    if (i < 0) {
        return;
    }
    int j = i + by;
    while (j >= 0 && j < (int)s_slots && !shown_on_home(&s_slot[j])) {
        j += by;
    }
    if (j < 0 || j >= (int)s_slots) {
        return;
    }
    slot_t moving = s_slot[i];
    if (j > i) {
        memmove(&s_slot[i], &s_slot[i + 1], sizeof(slot_t) * (size_t)(j - i));
    } else {
        memmove(&s_slot[j + 1], &s_slot[j], sizeof(slot_t) * (size_t)(i - j));
    }
    s_slot[j] = moving;
    changed();
}

/* The next size the module offers, round. */
static void on_size(lv_event_t *e)
{
    int i = slot_of(lv_event_get_user_data(e));
    if (i < 0) {
        return;
    }
    slot_t *sl = &s_slot[i];
    unsigned offered = sizes_of(sl->m);
    unsigned s = sl->size;
    do {
        s = s >= HUB_CARD_LARGE ? HUB_CARD_SMALL : s << 1;
    } while (!(s & offered));
    sl->size = (hub_card_size_t)s;
    changed();
}

static void on_done(lv_event_t *e)
{
    ui_dash_arrange(false);
}

/* What lies over a card while arranging: its name, the arrows, its size. */
static void make_cover(slot_t *sl)
{
    lv_obj_t *c = ui_box(sl->card);
    sl->cover = c;
    lv_obj_set_floating(c, true);
    lv_obj_set_size(c, HUB_CARD_W(sl->size), HUB_CARD_H);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(c, COL_SURFACE1, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, ui_radius(20), 0);
    lv_obj_set_style_border_color(c, COL_ACCENT_TEXT, 0);
    lv_obj_set_style_border_width(c, UI_S(2), 0);
    lv_obj_set_clickable(c, true); /* taps on a card don't reach it while arranging */
    ui_flex(c, LV_FLEX_FLOW_COLUMN, UI_S(16));
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *name = ui_label(c, &font_sora_16sb, COL_TEXT, sl->m->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(name, HUB_CARD_W(sl->size) - UI_S(24), 0);

    lv_obj_t *row = ui_box(c);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(8));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    void *m = (void *)sl->m;
    sl->prev = ui_btn_icon(row, UI_S(48), UI_S(48), 12, COL_SURFACE3, &font_icons_20, ICON_CHEVRON_LEFT, COL_TEXT,
                           NULL, false);
    lv_obj_set_user_data(sl->prev, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(sl->prev, on_move, LV_EVENT_CLICKED, m);
    sl->size_btn = ui_btn_text(row, UI_S(96), UI_S(48), 12, COL_SURFACE3, &font_sora_13m, COL_TEXT,
                               size_name(sl->size));
    lv_obj_add_event_cb(sl->size_btn, on_size, LV_EVENT_CLICKED, m);
    unsigned offered = sizes_of(sl->m);
    lv_obj_set_hidden(sl->size_btn, (offered & (offered - 1)) == 0); /* one size only */
    sl->next = ui_btn_icon(row, UI_S(48), UI_S(48), 12, COL_SURFACE3, &font_icons_20, ICON_CHEVRON_RIGHT, COL_TEXT,
                           NULL, false);
    lv_obj_set_user_data(sl->next, (void *)(intptr_t)1);
    lv_obj_add_event_cb(sl->next, on_move, LV_EVENT_CLICKED, m);
}

static void make_card(slot_t *sl)
{
    lv_obj_t *card = sl->m->create_card(s_cards, sl->size);
    if (!card) {
        return;
    }
    lv_obj_set_size(card, HUB_CARD_W(sl->size), HUB_CARD_H);
    lv_obj_set_clickable(card, true);
    lv_obj_add_event_cb(card, on_card, LV_EVENT_SHORT_CLICKED, (void *)sl->m);
    lv_obj_add_event_cb(card, on_card_long, LV_EVENT_LONG_PRESSED, NULL);
    sl->card = card;
    sl->built = sl->size;
    make_cover(sl);
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

    s_done = ui_btn_text(s_dash, UI_S(160), UI_S(56), 16, COL_ACCENT, &font_sora_16sb, COL_ON_ACCENT, "Done");
    lv_obj_align(s_done, LV_ALIGN_BOTTOM_RIGHT, -UI_S(16), -UI_S(16));
    lv_obj_set_style_shadow_width(s_done, UI_S(24), 0);
    lv_obj_set_style_shadow_color(s_done, COL_BG, 0);
    lv_obj_set_style_shadow_opa(s_done, LV_OPA_80, 0);
    lv_obj_add_event_cb(s_done, on_done, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(s_done, true);
    return s_dash;
}

/* Shows the cards of the modules that are on, in order, making any not
 * made yet or made at another size. */
void ui_dash_refresh(void)
{
    ensure_slots();
    if (!s_shown) {
        return;
    }
    size_t shown = 0;
    int first = -1, last = -1;
    for (size_t i = 0; i < s_slots; i++) {
        slot_t *sl = &s_slot[i];
        bool on = shown_on_home(sl);
        if (sl->card && sl->built != sl->size) {
            lv_obj_delete(sl->card);
            sl->card = NULL;
        }
        if (on && !sl->card) {
            make_card(sl);
        }
        if (sl->card) {
            lv_obj_move_to_index(sl->card, -1); /* in slot order */
            lv_obj_set_hidden(sl->card, !on);
            lv_obj_set_hidden(sl->cover, !s_arranging);
            if (on) {
                first = first < 0 ? (int)i : first;
                last = (int)i;
            }
        }
        shown += on && sl->card;
    }
    for (size_t i = 0; i < s_slots; i++) {
        if (s_slot[i].card) {
            lv_obj_set_hidden(s_slot[i].prev, (int)i == first);
            lv_obj_set_hidden(s_slot[i].next, (int)i == last);
        }
    }
    lv_obj_set_hidden(s_none, shown > 0);
    lv_obj_set_hidden(s_done, !s_arranging);
}

void ui_dash_arrange(bool on)
{
    s_arranging = on;
    ui_topbar_set_context(on ? "Arrange Home" : "Home");
    ui_dash_refresh();
}

void ui_dash_show(void)
{
    ui_topbar_set_context(s_arranging ? "Arrange Home" : "Home");
    s_shown = true;
    ui_dash_refresh();
}

/* Another view is coming: arranging ends. */
void ui_dash_leave(void)
{
    if (s_arranging) {
        s_arranging = false;
        ui_dash_refresh();
    }
}

/* The saved layout (ui_set_home_layout), or the modules changed. */
void ui_dash_set_layout(const char *layout)
{
    if (layout) {
        strlcpy(s_layout, layout, sizeof(s_layout));
    }
    build_slots();
    ui_dash_refresh();
}

void ui_dash_layout(char *out, size_t len)
{
    ensure_slots();
    layout_text(out, len);
}
