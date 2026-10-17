#pragma once

#include "hub_module.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_kit.h"

/* Layout (px) beyond ui_kit.h: top bar, then rail | center | pad. The pad
 * takes what is left of the width, so the row fills any screen. Figures
 * are at 1024 x 600. */
#define UI_RAIL_W   UI_S(112)
#define UI_CENTER_W UI_S(440)
#define UI_INNER_W  (UI_CENTER_W - UI_S(40)) /* the center panel's content, inside its padding */
#define UI_WIDE_INNER_W (UI_WIDE_W - UI_S(40)) /* 960: a full-width panel's content */
#define UI_PAD_W    (UI_WIDE_W - UI_RAIL_W - UI_CENTER_W - 2 * UI_GAP) /* 424 */
#define UI_STAGE_W  (UI_CENTER_W + UI_GAP + UI_PAD_W) /* 876: everything right of the rail */
#define UI_TV_PANEL_W (UI_WIDE_W - UI_GAP - UI_PAD_W)   /* 564: the IR TV's panel, beside the pad */

/* The screens. What each one leaves of the rail and the touchpad:
 * k_view_flags in ui.c. */
typedef enum {
    VIEW_SETUP = 0, /* first run, pairing */
    VIEW_TV_HOME,   /* Google TV's Home: the app grid */
    VIEW_APP,       /* the app on the TV */
    VIEW_KEYBOARD,
    VIEW_SETTINGS,
    VIEW_IR_TV,     /* the TV over IR */
    VIEW_IR_REMOTE, /* a learned remote */
    VIEW_LEARN,     /* the learn flow */
    VIEW_INPUTS,    /* the HDMI switch's inputs */
    VIEW_DASH,      /* the Home tab, one card per module */
    VIEW_MODULE,    /* a module's own screen */
    VIEW_TV_SETUP,  /* Add a TV, its keys */
    VIEW_COUNT
} ui_view_t;

typedef enum {
    UI_KEYMODE_TAP,    /* click: SHORT */
    UI_KEYMODE_REPEAT, /* click: SHORT; held: SHORT repeated */
    UI_KEYMODE_HOLD,   /* click: SHORT; held: START_LONG, release: END_LONG */
} ui_keymode_t;

/* --- Building blocks beyond ui_kit.h (ui_kit.c) ------------------------ */
void ui_btn_bind_key(lv_obj_t *btn, ui_key_t key, ui_keymode_t mode);
/* A hidden dot at the parent's top right corner (offset x, y), for an
 * update that waits there; show it with lv_obj_set_hidden(). */
lv_obj_t *ui_badge(lv_obj_t *parent, int32_t x, int32_t y);
/* text must outlive the button (string literal or static). */
void ui_btn_bind_action(lv_obj_t *btn, ui_action_type_t type, const char *text, int index);

/* --- Dispatch and views (ui.c) ------------------------------------------- */
void ui_emit(const ui_action_t *action);
void ui_emit_key(int key, ui_dir_t dir);
void ui_show_view(ui_view_t view);
ui_view_t ui_current_view(void);
/* Return from the keyboard or another device's screen to whatever the TV
 * is showing. */
void ui_return_to_context(void);
/* The rail's Keyboard key: opens the keyboard, or closes it. */
void ui_keyboard_toggle(void);
/* The remote tab: back to the remote last used (Google TV or an IR one). */
void ui_remote_return(void);
/* Switches the top bar between the TV and an IR device. */
void ui_show_ir_view(ui_view_t view, const char *device_name);
/* The Google TV's name and what it shows (current app, or why not). */
const char *ui_tv_display_name(void);
const char *ui_tv_meta(void);

/* --- Known apps (ui_apps.c) ----------------------------------------------- */
typedef struct {
    const char *package;
    const char *name;
    const lv_image_dsc_t *icon;       /* home tile */
    const lv_image_dsc_t *icon_small; /* app panel header */
    const char *link;                 /* deep link used to launch it */
    bool hides_title;                 /* the app never shares what is playing */
    bool on_tv;                       /* listed by the companion app: launch by package */
    bool no_skip;                     /* previous / next do nothing useful in it */
} ui_app_info_t;

/* The home grid's apps: the TV's (ui_set_apps) or else the built-in ones.
 * Icons may be NULL for TV apps. */
const ui_app_info_t *ui_apps_home(size_t *count);
void ui_apps_set_tv(const ui_app_t *apps, size_t count);
const ui_app_info_t *ui_apps_find(const char *package);
bool ui_apps_is_launcher(const char *package);
/* Home's layout (by package), kept by ui_apps.c. */
bool ui_apps_is_hidden(const char *package);
void ui_apps_set_hidden(const char *package, bool hidden);
/* Place in the user's order (smaller comes first), or -1 if never placed. */
int ui_apps_rank(const char *package);
void ui_apps_set_order(const char *const *packages, size_t count);
void ui_apps_load_layout(const char *order, const char *hidden);
void ui_apps_copy_layout(char *order, char *hidden);

/* --- The top bar (ui_topbar.c) -------------------------------------------- */
lv_obj_t *ui_topbar_create(lv_obj_t *parent);
/* Which tab looks selected: the remote tab, the Home tab, or neither (Settings). */
typedef enum { UI_TAB_REMOTE, UI_TAB_HOME, UI_TAB_NONE } ui_tab_t;
void ui_topbar_set_tab(ui_tab_t tab);
/* Shows the first module glance there is (e.g. the weather) in the top bar. */
void ui_topbar_refresh_glance(void);
void ui_topbar_set_context(const char *context);
void ui_topbar_set_target(const char *target);
/* The device icon in COL_IR while an IR device is on screen. */
void ui_topbar_set_ir(bool ir);
/* Battery levels: red in the top bar below UI_BATTERY_RED; the low-battery
 * card each time it falls below UI_BATTERY_WARN on battery. */
#define UI_BATTERY_RED  30
#define UI_BATTERY_WARN 20
void ui_topbar_set_battery(int percent, bool charging);
/* The dot on the Settings gear: an update (firmware or companion) waits. */
void ui_topbar_set_update_badge(bool on);
void ui_topbar_set_wifi(bool up);

/* --- The switcher sheet, on the top layer (ui_switcher.c) ----------------- */
void ui_switcher_build(void);
void ui_switcher_open(void);
void ui_switcher_close(void);
bool ui_switcher_is_open(void);

/* --- The Home tab and the module screen (ui_dash.c) ----------------------- */
lv_obj_t *ui_dash_create(lv_obj_t *parent);
void ui_dash_show(void);
/* Shows or hides cards after modules were switched on or off. */
void ui_dash_refresh(void);
lv_obj_t *ui_modview_create(lv_obj_t *parent);
const hub_module_t *ui_modview_current(void);
void ui_modview_leave(void);

/* --- The Streamer's screens: rail, pad, Home, app (ui_rail.c, ui_pad.c,
 *     ui_tv_home.c, ui_app.c) --------------------------------------------- */
lv_obj_t *ui_rail_create(lv_obj_t *parent);
/* The rail's keyboard key, highlighted while the keyboard is open. */
void ui_rail_set_kb(bool open);
void ui_rail_set_volume(int level, int max, bool muted);

lv_obj_t *ui_pad_create(lv_obj_t *parent);

lv_obj_t *ui_tv_home_create(lv_obj_t *parent);
/* Rebuilds the app grid from ui_apps_home(). */
void ui_tv_home_refresh(void);

lv_obj_t *ui_app_create(lv_obj_t *parent);
/* app == NULL: an app the remote doesn't know, shown by package name. */
void ui_app_set(const ui_app_info_t *app, const char *package);
void ui_app_set_media(const char *pkg, const char *title, const char *subtitle, bool playing);

/* --- The keyboard (ui_keyboard.c) ----------------------------------------- */
typedef enum {
    UI_KB_FIELD,  /* the TV reported a focused text field: edit its text */
    UI_KB_KEYS,   /* no field known: every key is pressed on the TV */
    UI_KB_LOCAL,  /* text for the remote itself (e.g. a name); nothing goes to the TV */
    UI_KB_SEARCH, /* on Google TV's Home: the text goes to Google TV's search */
} ui_kb_mode_t;

/* ui_kb_open_local() (local text entry) is in ui_kit.h. */
lv_obj_t *ui_kb_create(lv_obj_t *parent);
/* value/label: the field's text and hint (UI_KB_FIELD only). */
void ui_kb_open(ui_kb_mode_t mode, const char *value, const char *label);
/* The TV changed the field's text; ignored in UI_KB_KEYS. */
void ui_kb_set_text(const char *value);
/* Number fields get the number pad alone; passwords are masked. */
void ui_kb_set_field(ui_field_t field);
/* True while the keyboard is (or last was) open for the remote's own text. */
bool ui_kb_is_local(void);
/* A field came or went, or the TV changed app, while the keyboard is open:
 * switch how it reaches the TV, as opening it now would. Without this,
 * typing after the TV changed app went nowhere. */
void ui_kb_follow(ui_kb_mode_t mode);
/* Empties the typed text (the app or the focused field changed). */
void ui_kb_clear(void);
/* Closes the keyboard: cancels a local entry, otherwise back to the TV. */
void ui_kb_close(void);

/* --- Settings (ui_settings.c) --------------------------------------------- */
typedef enum { SEC_REMOTES, SEC_WIFI, SEC_SCREEN, SEC_APPEARANCE, SEC_MODULES, SEC_ABOUT, SEC_COUNT } ui_section_t;
lv_obj_t *ui_settings_create(lv_obj_t *parent);
void ui_settings_show_section(ui_section_t section);
void ui_settings_shown(void);
void ui_settings_show_remotes(void);
void ui_settings_show_appearance(void);
void ui_settings_set_tv(const char *name, ui_link_t link);
void ui_settings_set_companion(ui_companion_t state);
void ui_settings_set_companion_version(const char *version, ui_companion_update_t update, const char *next,
                                       bool can_install);
void ui_settings_set_turned(bool turned);
void ui_settings_set_brightness(int percent);
void ui_settings_set_timeouts(const ui_timeouts_t *t);
void ui_settings_set_hostname(const char *name);
void ui_settings_set_token(const char *token);
void ui_settings_set_update(ui_update_t state, const char *version, int percent);
/* Dots on the sections where an update waits: About (firmware), Remotes
 * (the companion). */
void ui_settings_set_update_badges(bool about, bool remotes);
void ui_settings_set_build(const char *version, const char *date, const char *time);
void ui_settings_set_about(const ui_about_t *about);
const char *ui_settings_version_text(void);
void ui_settings_modules_changed(void);
void ui_settings_set_time_zone(const char *name);
/* Opens the time-zone picker: the regions (region NULL), or one region's
 * zones ("Europe"). */
void ui_settings_pick_time_zone(const char *region);
void ui_settings_wifi_changed(void);
/* ui_set_ir_caps(): what the IR wiring allows (learning implies sending),
 * and the screens that hide what it doesn't. */
bool ui_ir_can_send(void);
bool ui_ir_can_learn(void);
void ui_settings_ir_caps_changed(void);
/* ui_set_board_caps(): Settings > Screen and the top bar follow it. */
void ui_settings_board_caps(bool dims, bool battery);
void ui_topbar_show_battery(bool shown);
void ui_ir_caps_changed(void);
void ui_tv_setup_ir_caps_changed(void);
void ui_tv_home_ir_caps_changed(void);

/* The IR TV changed (ui_set_ir_tv): its row and the volume choice. */
void ui_settings_tv_changed(void);
/* The learned remotes' names, kept here for Settings, the switcher and the
 * learn flow. */
void ui_settings_set_remotes(const char *const *names, size_t count);
size_t ui_settings_remote_count(void);
const char *ui_settings_remote_name(size_t index);
/* Opens learned remote n, as tapping its row does. */
void ui_settings_open_remote(int index);

/* --- Themes (ui_themes.c) ------------------------------------------------- */
size_t ui_theme_count(void);
const ui_theme_t *ui_theme_at(size_t index);

/* --- The IR TV and learned remotes (ui_ir.c) ------------------------------ */
lv_obj_t *ui_ir_tv_create(lv_obj_t *parent);
/* The IR TV's state, from ui_set_ir_tv(). */
void ui_ir_tv_update(const char *name, const char *detail, const char *const *keys, size_t count, bool volume_ir);
/* The IR TV panel's mute key shows the TV's mute state too. */
void ui_ir_tv_set_muted(bool muted);
bool ui_irtv_configured(void);
const char *ui_irtv_name(void);
const char *ui_irtv_detail(void);
bool ui_irtv_volume(void);
bool ui_irtv_has(const char *key);
void ui_irtv_show(void);
lv_obj_t *ui_ir_remote_create(lv_obj_t *parent);
void ui_ir_remote_set(const char *name, const ui_ir_button_t *buttons, size_t count);
/* Index into the remotes list of the remote now on screen, -1 if none.
 * Another index ends editing. */
int ui_ir_remote_index(void);
void ui_ir_remote_set_index(int index);
/* The learned remote's screen was left: ends editing. */
void ui_ir_remote_leave(void);

/* --- Add a TV and its keys (ui_tv_setup.c) -------------------------------- */
lv_obj_t *ui_tv_setup_create(lv_obj_t *parent);
void ui_tv_setup_open(void);      /* brands */
void ui_tv_setup_open_keys(void); /* every key: learn the missing ones */
void ui_tv_setup_set_brands(const char *const *brands, size_t count);
void ui_tv_setup_show_sets(const char *brand, const char *const *test_keys, size_t count);
/* "Volume up" for "vol_up". */
const char *ui_irtv_key_label(const char *key);

/* --- The learn flow (ui_learn.c) ------------------------------------------ */
lv_obj_t *ui_learn_create(lv_obj_t *parent);
/* The learned remotes changed (ui_settings_set_remotes(), which keeps them). */
void ui_learn_remotes_changed(void);
/* Opens the learn flow at its first step; preselects remote n (-1: new). */
void ui_learn_open(int remote_index);
/* Learn flow for one Inputs page input: goes straight to capturing. */
void ui_learn_open_input(int input, const char *name);
void ui_learn_end_input(void);
/* Learns one key of the IR TV, then returns to its panel. */
void ui_learn_open_tv_key(const char *key);
void ui_learn_show_progress(int presses, int needed, bool mismatch);
void ui_learn_show_result(const char *summary);
void ui_learn_show_failed(const char *reason);

/* --- The HDMI switch's inputs (ui_inputs.c) ------------------------------- */
lv_obj_t *ui_inputs_create(lv_obj_t *parent);
void ui_inputs_set(const char *const *names, const bool *learned);
void ui_inputs_show(void);

/* --- Wi-Fi network pickers (ui_wifi.c): one in setup, one in Settings ----- */
/* Each returns to `origin` after the keyboard. */
typedef enum { UI_WIFI_SETUP, UI_WIFI_SETTINGS, UI_WIFI_PICKERS } ui_wifi_picker_t;
lv_obj_t *ui_wifi_picker_create(lv_obj_t *parent, ui_wifi_picker_t which, ui_view_t origin);
void ui_wifi_picker_message(ui_wifi_picker_t which, const char *message);
void ui_wifi_scan(void);
void ui_wifi_show_networks(const ui_wifi_ap_t *aps, size_t count);
void ui_wifi_set_current(const char *ssid);
const char *ui_wifi_current(void);
bool ui_wifi_is_up(void);

/* --- First run and code entry (ui_setup.c) -------------------------------- */
lv_obj_t *ui_setup_create(lv_obj_t *parent);
void ui_setup_show_busy(const char *title, const char *body);
void ui_setup_show_wifi(const char *message);
void ui_setup_show_tvs(const ui_tv_t *tvs, size_t count);
void ui_setup_show_code(const char *tv_name);
void ui_setup_show_code_wrong(void);
/* The same code entry for the companion app's code, with Cancel. */
void ui_setup_show_companion_code(void);
/* Clears the entry and says why (a code that expired, no answer). */
void ui_setup_show_code_message(const char *message);
void ui_setup_show_error(const char *title, const char *body);
