#pragma once

/*
 * The look of the remote, for modules and the rest of the UI: layout sizes,
 * colors, fonts, icons and building blocks. Build module screens from these
 * so they match the rest of the remote and follow its theme (docs/themes.md).
 *
 * Every function here touches LVGL: call it on the LVGL task (a module's
 * create_view, create_card, show and glance callbacks, or hub_ui_async).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_size.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layout (px): the screen (UI_W x UI_H, ui_size.h), a 56 px top bar and
 * 12 px gaps at the design size. Write sizes as UI_S(design px). */
#define UI_TOPBAR_H UI_S(56)
#define UI_GAP      UI_S(12)
#define UI_BODY_H   (UI_H - UI_TOPBAR_H - UI_GAP) /* 532 at 1024 x 600 */
#define UI_WIDE_W   (UI_W - 2 * UI_GAP)           /* 1000: the whole width */

/*
 * Colors come from the active theme (ui_themes.c; docs/themes.md), by role.
 * Themes are chosen in Settings > Appearance and applied at start-up, so a
 * color read while building a screen stays right for its lifetime.
 */
typedef struct {
    const char *id;   /* "midnight": stored in NVS, stable */
    const char *name; /* "Midnight": shown in Settings */
    bool light;       /* dark text on light surfaces */
    bool square;      /* small corner radii instead of round ones */
    lv_color_t bg;                            /* behind everything */
    lv_color_t surface1, surface2, surface3;  /* panels; tiles and buttons on panels; pressed or selected */
    lv_color_t line, line2;                   /* dividers and outlines */
    lv_color_t text, text2, text3;            /* main, secondary, faint */
    lv_color_t accent, on_accent;             /* filled buttons, sliders, switches, and text on them */
    lv_color_t accent_text;                   /* the accent as text or an icon on a surface */
    lv_color_t ir, on_ir, ir_line;            /* IR devices */
    lv_color_t good, bad;                     /* charging, errors */
    lv_color_t sun, sun_soft, rain, storm, cool, warm, moon; /* weather and other data */
} ui_theme_t;

const ui_theme_t *ui_theme(void);

#define COL_BG          (ui_theme()->bg)
#define COL_SURFACE1    (ui_theme()->surface1)
#define COL_SURFACE2    (ui_theme()->surface2)
#define COL_SURFACE3    (ui_theme()->surface3)
#define COL_LINE        (ui_theme()->line)
#define COL_LINE2       (ui_theme()->line2)
#define COL_TEXT        (ui_theme()->text)
#define COL_TEXT2       (ui_theme()->text2)
#define COL_TEXT3       (ui_theme()->text3)
#define COL_ACCENT      (ui_theme()->accent)      /* fills: Done, Play, sliders, switches */
#define COL_ON_ACCENT   (ui_theme()->on_accent)   /* text and icons on COL_ACCENT */
#define COL_ACCENT_TEXT (ui_theme()->accent_text) /* accent-colored text, icons, outlines */
#define COL_IR          (ui_theme()->ir)
#define COL_ON_IR       (ui_theme()->on_ir)
#define COL_IR_LINE     (ui_theme()->ir_line)
#define COL_GOOD        (ui_theme()->good)
#define COL_BAD         (ui_theme()->bad)
#define COL_SUN         (ui_theme()->sun)
#define COL_SUN_SOFT    (ui_theme()->sun_soft)
#define COL_RAIN        (ui_theme()->rain)
#define COL_STORM       (ui_theme()->storm)
#define COL_COOL        (ui_theme()->cool)
#define COL_WARM        (ui_theme()->warm)
#define COL_MOON        (ui_theme()->moon)

/* Corner radius r (design px) as drawn in the active theme and on this
 * screen: smaller in square themes, scaled like UI_S(); LV_RADIUS_CIRCLE
 * stays round. Use it for every radius (ui_btn() and ui_panel() do). */
int32_t ui_radius(int32_t r);
/* The pressed shade of a background color (lighter in dark themes, darker
 * in light ones). */
lv_color_t ui_pressed(lv_color_t bg);

/* Building blocks. */
lv_obj_t *ui_box(lv_obj_t *parent);                         /* invisible, not scrollable, not clickable */
lv_obj_t *ui_panel(lv_obj_t *parent, int32_t w, int32_t h); /* SURFACE1, ui_radius(20) corners, 20 px padding */
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);
/* A label as wide as its parent that wraps onto more lines. */
lv_obj_t *ui_label_wrap(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);
lv_obj_t *ui_btn(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius, lv_color_t bg);
/* Button with an icon and an optional caption, stacked or side by side. */
lv_obj_t *ui_btn_icon(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius, lv_color_t bg,
                      const lv_font_t *icon_font, const char *icon, lv_color_t fg, const char *caption,
                      bool vertical);
lv_obj_t *ui_btn_text(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius, lv_color_t bg,
                      const lv_font_t *font, lv_color_t fg, const char *text);
void ui_flex(lv_obj_t *obj, lv_flex_flow_t flow, int32_t gap);

/* A spinning arc for loading states, size x size px, running. Turn it off
 * with ui_spinner_run(spinner, false): that hides it and stops its
 * animation, so an idle spinner costs nothing. */
lv_obj_t *ui_spinner(lv_obj_t *parent, int32_t size, lv_color_t color);
void ui_spinner_run(lv_obj_t *spinner, bool run);

/*
 * Loading, error and empty states, with one look on every screen: use this
 * whenever a screen or card has nothing to show yet. Make it once, cover
 * the area with it, then switch states as data arrives:
 *
 *     s_status = ui_status_create(parent, HUB_VIEW_W, HUB_VIEW_H, false);
 *     ui_status_add_action(s_status, UI_STATUS_ERROR, ICON_REFRESH, "Try again", true, on_retry, NULL);
 *     ...
 *     ui_status_set(s_status, UI_STATUS_LOADING, place_name, NULL);
 *     ui_status_set(s_status, UI_STATUS_ERROR, "Couldn't load the forecast", place_name);
 *     ui_status_set(s_status, UI_STATUS_NONE, NULL, NULL);  // hidden: show the content
 *
 * Full size: a panel with the spinner or icon above the text and buttons,
 * centered. Compact (for cards): transparent, icon or spinner left of the
 * text. While data is already on screen, don't cover it to refresh; show a
 * small ui_spinner next to its "Updated" time instead.
 */
typedef enum {
    UI_STATUS_NONE,    /* hidden */
    UI_STATUS_LOADING, /* spinner */
    UI_STATUS_ERROR,   /* alert icon (COL_ACCENT_TEXT) unless set otherwise */
    UI_STATUS_EMPTY,   /* nothing set up yet: no icon unless set */
} ui_status_t;

lv_obj_t *ui_status_create(lv_obj_t *parent, int32_t w, int32_t h, bool compact);
/* title and detail may be NULL. */
void ui_status_set(lv_obj_t *status, ui_status_t state, const char *title, const char *detail);
/* The icon shown in an ERROR or EMPTY state (any icon font, e.g. font_weather_96). */
void ui_status_set_icon(lv_obj_t *status, ui_status_t state, const lv_font_t *font, const char *icon,
                        lv_color_t color);
/* A button shown in one state only. primary: filled with COL_ACCENT, otherwise plain.
 * icon may be NULL. Returns the button. */
lv_obj_t *ui_status_add_action(lv_obj_t *status, ui_status_t state, const char *icon, const char *text,
                               bool primary, lv_event_cb_t cb, void *user_data);

/* A short message in a pill under the top bar. */
void ui_toast(const char *icon, const char *text);

/* The on-screen keyboard for text the remote itself needs (a name, a city).
 * done gets the text on Enter; cancel runs when it's closed. Afterwards,
 * show your screen again (e.g. ui_module_open()). */
typedef void (*ui_kb_done_cb_t)(const char *text);
typedef void (*ui_kb_cancel_cb_t)(void);
void ui_kb_open_local(const char *value, const char *title, size_t max_len, ui_kb_done_cb_t done,
                      ui_kb_cancel_cb_t cancel);

/*
 * Dialogs over the whole screen, one at a time: opening one closes the one
 * before it. done runs after the dialog has closed, with the index of the
 * button or item tapped, or -1 when it was dismissed (Cancel, a tap
 * outside it, or another dialog opening). done may open another dialog.
 *
 *  - ui_modal(): a title, a message (may be NULL), and 1 to 3 buttons left
 *    to right; the last is the main one, filled with COL_ACCENT.
 *  - ui_modal_list(): a title and items to pick from, scrolling when long,
 *    with Cancel. selected (or -1) is marked and scrolled into view.
 *
 * The texts are copied.
 */
typedef void (*ui_modal_cb_t)(int choice, void *user_data);
void ui_modal(const char *title, const char *message, const char *const *buttons, size_t count,
              ui_modal_cb_t done, void *user_data);
void ui_modal_list(const char *title, const char *const *items, size_t count, int selected,
                   ui_modal_cb_t done, void *user_data);
/* Closes the open dialog, if any (its done gets -1). */
void ui_modal_close(void);
bool ui_modal_open(void);

/* Opens a module's screen by id. */
void ui_module_open(const char *id);

#ifdef __cplusplus
}
#endif
