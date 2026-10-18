#pragma once

/*
 * The remote's UI. A top bar (the current remote and its switcher, the Home
 * tab of module cards, context, a module's glance, Wi-Fi, battery,
 * Settings) over the current view. For the Streamer: a rail on the left
 * (Power, Back, Home, Keyboard, volume), a panel in the middle that follows
 * the app on the TV, and a touchpad or D-pad on the right. Other views: the
 * IR TV, learned remotes, Inputs, the learn flow, Settings, modules. Colors
 * come from the active theme (ui_themes.c).
 *
 * Every function here is safe to call from any task: each takes the LVGL
 * port lock itself. The action callback runs on the LVGL task with that lock
 * held, so it must only hand the action off (copy it into a queue).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ui_size.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Android KeyEvent codes the UI emits (same values as KEYCODE_*). */
typedef enum {
    UI_KEY_HOME = 3,
    UI_KEY_BACK = 4,
    UI_KEY_0 = 7, /* UI_KEY_0 + n for digit n */
    UI_KEY_DPAD_UP = 19,
    UI_KEY_DPAD_DOWN = 20,
    UI_KEY_DPAD_LEFT = 21,
    UI_KEY_DPAD_RIGHT = 22,
    UI_KEY_DPAD_CENTER = 23,
    UI_KEY_VOLUME_UP = 24,
    UI_KEY_VOLUME_DOWN = 25,
    UI_KEY_POWER = 26,
    UI_KEY_A = 29, /* UI_KEY_A + n for letter n */
    UI_KEY_SHIFT_LEFT = 59,
    UI_KEY_ENTER = 66,
    UI_KEY_DEL = 67,
    UI_KEY_MEDIA_PLAY_PAUSE = 85,
    UI_KEY_MEDIA_NEXT = 87,
    UI_KEY_MEDIA_PREVIOUS = 88,
    UI_KEY_VOLUME_MUTE = 164,
    UI_KEY_CAPTIONS = 175,
    UI_KEY_SETTINGS = 176,
    UI_KEY_TV_INPUT = 178,
} ui_key_t;

/* Same values as the protocol's RemoteDirection. */
typedef enum {
    UI_DIR_START_LONG = 1,
    UI_DIR_END_LONG = 2,
    UI_DIR_SHORT = 3,
} ui_dir_t;

typedef enum {
    UI_ACT_KEY = 0,      /* .key, .dir */
    UI_ACT_LAUNCH,       /* .text = app deep link */
    UI_ACT_LAUNCH_APP,   /* .text = package name (an app from ui_set_apps()) */
    UI_ACT_APP_LAYOUT,   /* the user hid, showed or moved apps on Home: save ui_app_layout() */
    UI_ACT_MEDIA_KEYS,   /* the user showed or hid an app's playback keys: save ui_media_apps() */
    UI_ACT_INPUT,        /* .index = HDMI switch input (0..UI_INPUTS-1): send its code */
    UI_ACT_INPUT_RENAME, /* .index, .text = the input's new name */
    UI_ACT_INPUT_LEARN,  /* .index: learn that input's code; progress via ui_learn_* */
    UI_ACT_TEXT,         /* .text = the whole field text after an edit */
    UI_ACT_TYPE,         /* .text = characters typed with no field known (key mode): insert them */
    UI_ACT_UPDATE_CHECK,   /* Settings > About: look for a newer release */
    UI_ACT_UPDATE_INSTALL, /* install it (the remote restarts) */
    UI_ACT_SEARCH,       /* .text = Google TV search text typed on Home ("" cancels);
                          * .index = 1 search now (Enter), 0 once typing pauses */
    UI_ACT_ENTER,        /* Enter in a text field (its own action if the companion knows it) */
    UI_ACT_KEYBOARD_SHOWN, /* .index = 1 the remote's keyboard opened, 0 it closed */
    UI_ACT_PAIR_CODE,    /* .text = the 6 hex characters typed */
    UI_ACT_PAIR_RESTART, /* ask the TV for a new code */
    UI_ACT_PICK_TV,      /* .index into the list passed to ui_setup_tvs() */
    UI_ACT_RESCAN,       /* search the network for TVs again */
    UI_ACT_RETRY,        /* the error screen's retry button */
    UI_ACT_REPAIR,       /* forget the pairing and pair again */
    UI_ACT_BRIGHTNESS,   /* .index = screen brightness, 1..100 % (save it) */
    UI_ACT_SCREEN_TURN,  /* .index = 1: turn the screen and touch 180 degrees, 0: upright (save it) */
    UI_ACT_IR,           /* UI_TARGET_IR_TV: .text = TV key ("power_on", "hdmi1" ...);
                            UI_TARGET_LEARNED: .index = button slot */
    UI_ACT_OPEN_REMOTE,  /* .index: learned remote n of ui_set_ir_remotes() */
    UI_ACT_REMOTE_RENAME, /* the learned remote on screen: .text = its new name */
    UI_ACT_REMOTE_DELETE, /* the learned remote on screen: delete it (ui_learned_remote_deleted()) */
    UI_ACT_BUTTON_RENAME, /* the learned remote on screen: .index = button slot, .text = its new label */
    UI_ACT_BUTTON_DELETE, /* the learned remote on screen: .index = button slot */
    UI_ACT_LEARN_START,  /* .index: n >= 0 adds to learned remote n, -1 = new remote named .name;
                            .text = the button's label */
    UI_ACT_LEARN_CANCEL,
    UI_ACT_LEARN_TEST,   /* send the code just learned */
    UI_ACT_LEARN_SAVE,   /* store the code just learned */
    UI_ACT_POWER_OFF,    /* turn the remote off (deep sleep) */
    UI_ACT_DIM_AFTER,        /* .index = idle seconds before the screen dims */
    UI_ACT_SCREEN_OFF_AFTER, /* .index = idle seconds before the screen goes dark */
    UI_ACT_DIM_AFTER_CHARGING,       /* the same while on USB power; .index 0 = never */
    UI_ACT_SCREEN_OFF_AFTER_CHARGING,
    UI_ACT_SLEEP_AFTER,      /* .index = idle hours before the remote turns off, 0 = never */
    UI_ACT_MODULE_ENABLE,    /* .text = module id, .index = 1 on, 0 off */
    UI_ACT_TIME_ZONE,        /* .text = IANA name, "" = Automatic: set it, then ui_set_time_zone() */
    UI_ACT_MODULE_SETTING,   /* .name = "<module id>/<key>", .index = value, .text = a TEXT setting's value */
    UI_ACT_THEME,            /* .text = theme id: save it and restart to apply */
    UI_ACT_TV_BRAND,         /* .text = brand: send its code sets with ui_tv_setup_sets() */
    UI_ACT_TV_TRY,           /* .index = code set n of that brand: send its test key */
    UI_ACT_TV_CHOOSE,        /* .index = code set n of that brand: use it */
    UI_ACT_TV_LEARNED,       /* a TV whose keys are all learned from its remote */
    UI_ACT_TV_REMOVE,
    UI_ACT_TV_VOLUME,        /* .index = 1: volume keys go to the TV's IR, 0: the Streamer */
    UI_ACT_TV_LEARN_KEY,     /* .text = TV key: learn it; progress via ui_learn_* */
    UI_ACT_WIFI_SCAN,        /* look for Wi-Fi networks: answer with ui_set_wifi_networks() */
    UI_ACT_WIFI_JOIN,        /* .name = network name, .text = password ("" for an open network) */
    UI_ACT_COMPANION_PAIR,   /* show a pairing code on the TV: answer with ui_companion_pairing() */
    UI_ACT_COMPANION_CODE,   /* .text = the code typed: answer with ui_companion_pair_result() */
    UI_ACT_COMPANION_CANCEL, /* code entry closed: take the code off the TV */
    UI_ACT_COMPANION_UPDATE, /* update the companion to the release's version */
} ui_action_type_t;

/* Which device a key press is for: decided by the screen it came from. */
typedef enum {
    UI_TARGET_STREAMER = 0, /* Google TV Streamer (network) */
    UI_TARGET_IR_TV,        /* the TV over IR (a code set and learned keys) */
    UI_TARGET_LEARNED,      /* the learned IR remote on screen */
} ui_target_t;

typedef struct {
    ui_action_type_t type;
    ui_target_t target;
    int key;
    ui_dir_t dir;
    const char *text; /* valid only during the callback */
    const char *name; /* valid only during the callback */
    int index;
} ui_action_t;

typedef void (*ui_action_cb_t)(const ui_action_t *action, void *ctx);

typedef enum {
    UI_LINK_OFFLINE = 0, /* no Wi-Fi or no TV session */
    UI_LINK_CONNECTING,
    UI_LINK_CONNECTED,
} ui_link_t;

typedef struct {
    char name[64];
    char host[40];
} ui_tv_t;

/* The theme to build with (its id, e.g. "daylight"; unknown ids keep
 * Midnight). Call before ui_init: themes apply at start-up. */
void ui_set_theme(const char *id);

/* Builds every view on the default display. Starts on the setup view. */
void ui_init(ui_action_cb_t cb, void *ctx);

/* --- Live state ---------------------------------------------------------- */
void ui_set_link(ui_link_t link);
void ui_set_tv_name(const char *name);
void ui_set_tv_power(bool on);
/* Foreground app package from the TV. Switches the context panel. */
void ui_set_app(const char *package);
/* What is playing, from the companion (0.6+): the app's package, the title
 * and subtitle ("" if the app doesn't share them), and whether it plays
 * (else paused). pkg NULL or "": nothing known. The app panel shows it
 * while that app is in front. */
void ui_set_media(const char *pkg, const char *title, const char *subtitle, bool playing);
/* System screens that cover the app for a while (the screensaver, the
 * Assistant, system panels, keyboards): not an app to show. No lock. */
bool ui_app_is_system_screen(const char *package);
void ui_set_volume(int level, int max, bool muted);
/* A text field on the TV got focus: open the keyboard with its text. */
void ui_ime_show(const char *value, const char *label);
/* The TV reported the field's text (e.g. after its own edit). */
void ui_ime_text(const char *value);

/* What has input focus on the TV, from the companion app. */
typedef enum {
    UI_FIELD_UNKNOWN = 0, /* no companion app: the UI guesses */
    UI_FIELD_NONE,
    UI_FIELD_TEXT,
    UI_FIELD_PASSWORD, /* the keyboard hides what is typed */
    UI_FIELD_NUMBER,   /* the keyboard shows only the number pad */
} ui_field_t;
/* id changes whenever a different field gets focus. */
void ui_set_field(ui_field_t field, int id);

/* The companion app: not found, found but turning this remote away until
 * it pairs (companion 0.4+), or working. */
typedef enum {
    UI_COMPANION_NONE = 0,
    UI_COMPANION_UNPAIRED,
    UI_COMPANION_ON,
} ui_companion_t;
void ui_set_companion(ui_companion_t state);
/* The answer to UI_ACT_COMPANION_PAIR: the code is on the TV (opens code
 * entry), or the companion couldn't be reached. */
/* locked_s: when no code was shown because of too many wrong ones, the
 * wait in seconds (0: the companion didn't answer). */
void ui_companion_pairing(bool code_shown, uint32_t locked_s);
typedef enum {
    UI_PAIR_OK = 0,
    UI_PAIR_WRONG,   /* type it again */
    UI_PAIR_EXPIRED, /* no code on the TV any more: show a new one */
    UI_PAIR_FAILED,  /* no answer */
} ui_pair_result_t;
/* The answer to UI_ACT_COMPANION_CODE. */
void ui_companion_pair_result(ui_pair_result_t result);
/* The companion's version on the TV ("0.5"; "" if unknown) and its update,
 * for Settings > Remotes. next: the release's version. can_install: the
 * companion updates itself (0.5+), so Update companion is offered. */
typedef enum {
    UI_COMPANION_UPDATE_NONE = 0, /* the newest, or nothing known */
    UI_COMPANION_UPDATE_AVAILABLE,
    UI_COMPANION_UPDATE_RUNNING,  /* downloading or installing */
    UI_COMPANION_UPDATE_CONFIRM,  /* Android's install dialog is up on the TV */
    UI_COMPANION_UPDATE_FAILED,
} ui_companion_update_t;
void ui_set_companion_version(const char *version, ui_companion_update_t update, const char *next, bool can_install);

/* Apps installed on the TV (from the companion app). Icons are LVGL
 * RGB565A8 pixels, UI_APP_ICON / UI_APP_ICON_SMALL square, or NULL; they
 * must stay valid until the next ui_set_apps() returns. count 0 brings
 * back the built-in apps. */
#define UI_MAX_APPS       48
#define UI_APP_ICON       UI_S(112) /* px; ui_size.h */
#define UI_APP_ICON_SMALL UI_S(44)
#define UI_APP_PKG_MAX    96 /* a package name, with its terminator */
#define UI_APP_NAME_MAX   48
typedef struct {
    const char *pkg;
    const char *label;
    const uint8_t *icon;
    const uint8_t *icon_small;
} ui_app_t;
void ui_set_apps(const ui_app_t *apps, size_t count);

/* Home's app layout as saved: the user's order and the hidden apps, each a
 * list of package names between newlines ("\npkg.a\npkg.b\n"). Apps never
 * placed follow the ordered ones, in list order. */
#define UI_APP_LIST_MAX 3072
void ui_set_app_layout(const char *order, const char *hidden);
/* Copies both lists (UI_APP_LIST_MAX bytes each) for saving. */
void ui_app_layout(char *order, char *hidden);

/* Which apps get playback keys: the media apps the remote knows, apps that
 * have played something (ui_learn_media_app), and apps the user switched
 * on; never the apps the user switched off. Lists as Home's layout. */
#define UI_MEDIA_LIST_MAX 1024
void ui_set_media_apps(const char *on, const char *off);
/* Copies both lists (UI_MEDIA_LIST_MAX bytes each) for saving. */
void ui_media_apps(char *on, char *off);
/* An app is playing something: it gets playback keys from now on, unless
 * the user hid them. True if the lists changed (save them). */
bool ui_learn_media_app(const char *package);
void ui_set_battery(int percent, bool charging);
void ui_set_wifi(bool up);
/* Settings > Screen: the brightness slider (1..100 %), and the idle times
 * to show as chosen: seconds on battery, seconds while charging (0 =
 * never), and hours before the remote turns off (0 = never). */
void ui_set_brightness(int percent);
/* Settings > Screen > Rotate 180°, as it is. */
void ui_set_screen_turned(bool turned);
typedef struct {
    int dim_s, off_s;                   /* on battery */
    int dim_charging_s, off_charging_s; /* on USB power */
    int sleep_h;
} ui_timeouts_t;
void ui_set_screen_timeouts(const ui_timeouts_t *t);
/* What the board can do (components/board): dim its backlight (without,
 * Settings has no Brightness or Dim after), and read its battery (without,
 * the top bar shows no battery and Settings no On battery / Charging
 * switch: the battery times apply). Both on until called. */
void ui_set_board_caps(bool dims, bool battery);
/* Settings > About: the remote's network name, e.g. "hubmote.local". */
void ui_set_hostname(const char *name);
/* The time zone in Settings > Modules: an IANA name, "" = Automatic. */
void ui_set_time_zone(const char *name);
/* Settings > About: the token updates and developer tools need (shown on
 * request). */
void ui_set_update_token(const char *token);
/* Updates from the published releases, for Settings > About. */
typedef enum {
    UI_UPDATE_UNKNOWN = 0,
    UI_UPDATE_CHECKING,
    UI_UPDATE_CURRENT,
    UI_UPDATE_AVAILABLE,  /* version: the newer release */
    UI_UPDATE_INSTALLING, /* percent done */
    UI_UPDATE_FAILED,
} ui_update_t;
void ui_set_update(ui_update_t state, const char *version, int percent);
/* When the firmware was built ("Oct 13 2026", "23:10"), for Settings. */
void ui_set_build(const char *version, const char *date, const char *time);
/* The rest of Settings > About, from the controller every few seconds. */
typedef struct {
    char slot[16];       /* the running app slot ("ota_0"), "" unknown */
    char address[16];    /* IPv4 address, "" while offline */
    int rssi;            /* Wi-Fi signal in dBm, 0 unknown */
    uint32_t uptime_min;
    uint32_t internal_kb; /* free internal RAM */
    uint32_t psram_kb;    /* free PSRAM */
} ui_about_t;
void ui_set_about(const ui_about_t *about);

/* While asleep a transparent layer swallows the first tap, so waking the
 * screen never presses whatever was under the finger. */
void ui_set_asleep(bool asleep);
/* Milliseconds since the last touch. */
uint32_t ui_idle_ms(void);

/* --- First run and pairing (shown instead of the controls) --------------- */
void ui_setup_busy(const char *title, const char *body);
/* First run without Wi-Fi: choose a network. message (or NULL) says why
 * the last one didn't work. Starts a scan (UI_ACT_WIFI_SCAN). */
void ui_setup_wifi(const char *message);

typedef struct {
    char ssid[33];
    int rssi;    /* dBm */
    bool secure; /* needs a password */
} ui_wifi_ap_t;
#define UI_WIFI_APS_MAX 24 /* networks a list shows */
/* Networks found, strongest first (count 0: none, or the scan failed). */
void ui_set_wifi_networks(const ui_wifi_ap_t *aps, size_t count);
/* The network the remote uses, for Settings > Wi-Fi ("" if none). */
void ui_set_wifi_network(const char *ssid);
void ui_setup_tvs(const ui_tv_t *tvs, size_t count);
void ui_setup_code(const char *tv_name);
void ui_setup_code_wrong(void);
void ui_setup_error(const char *title, const char *body);
/* Leave setup and show the controls. */
void ui_show_main(void);
/* A short message over whatever is on screen; icon from ui_icons.h. */
void ui_notify(const char *icon, const char *text);
/* Lights the screen as a touch would (it dims and goes dark again later). */
void ui_wake(void);
/* Opens Settings > Appearance (after a restart for a new theme). */
void ui_show_appearance(void);

/* --- IR devices ------------------------------------------------------------ */

/* The Inputs page: the HDMI switch's inputs. names/learned have UI_INPUTS
 * entries; an empty name shows as "Input N". */
#define UI_INPUTS 4
#define UI_INPUT_NAME_MAX 20 /* with its terminator */
void ui_set_inputs(const char *const *names, const bool *learned);
/* Back to the Inputs page (after an input was learned and saved). */
void ui_show_inputs(void);
#define UI_IR_LABEL_MAX 20 /* a button's label, with its terminator */
typedef struct {
    char label[UI_IR_LABEL_MAX];
    uint8_t slot; /* grid position, 0..23 (4 columns x 6 rows) */
} ui_ir_button_t;

/* Learned remotes, listed in Settings > Remotes, the switcher and the learn flow. */
#define UI_IR_MAX_REMOTES 12
#define UI_IR_NAME_MAX    32 /* with its terminator */
void ui_set_ir_remotes(const char *const *names, size_t count);

/* --- The IR TV ------------------------------------------------------------- */
/* The TV over IR: its name ("" = none set up), a detail line, the keys it
 * has ("power", "hdmi1" ...; the panel dims the others and offers to learn
 * them), and whether volume keys go to its IR. */
void ui_set_ir_tv(const char *name, const char *detail, const char *const *keys, size_t count, bool volume_ir);
/* What the board's IR wiring allows (menuconfig > Hubmote > IR): sending,
 * and learning (a receiver too). Without sending, the switcher, Settings
 * and Home leave IR out; without learning, whatever learns a code does
 * (Learn buttons, Add a remote, Inputs). Both on until called. */
void ui_set_ir_caps(bool send, bool learn);
/* Brands with built-in code sets, for Add a TV (at most UI_TV_BRANDS_MAX). */
#define UI_TV_BRANDS_MAX 48
#define UI_TV_SETS_MAX   16
#define UI_TV_KEYS_MAX   40
void ui_set_tv_brands(const char *const *brands, size_t count);
/* The brand's code sets to try, each with the key it is tried with. */
void ui_tv_setup_sets(const char *brand, const char *const *test_keys, size_t count);
void ui_show_ir_tv(void);
/* Opens learned remote `index` (its position in ui_set_ir_remotes()). */
void ui_show_learned_remote(int index, const char *name, const ui_ir_button_t *buttons, size_t count);
/* The learned remote on screen was deleted: Settings > Remotes. */
void ui_learned_remote_deleted(void);
/* Learn flow: progress while capturing, the result, or a failure that
 * returns to the first step with a message. */
void ui_learn_progress(int presses, int needed, bool mismatch);
void ui_learn_result(const char *summary);
void ui_learn_failed(const char *reason);

#ifdef __cplusplus
}
#endif
