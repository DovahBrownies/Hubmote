#pragma once

/*
 * What modules can do on the TV, and what it is playing.
 *
 * The actions queue a request for the controller and return at once: call
 * them from any task. They return ESP_ERR_INVALID_STATE before the platform
 * is up, and ESP_ERR_NO_MEM when its queue is full. What the TV made of
 * them shows in the log.
 */

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "hub_module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HUB_KEY_HOME,
    HUB_KEY_BACK,
    HUB_KEY_UP,
    HUB_KEY_DOWN,
    HUB_KEY_LEFT,
    HUB_KEY_RIGHT,
    HUB_KEY_OK,
    HUB_KEY_PLAY_PAUSE,
    HUB_KEY_VOLUME_UP,   /* volume keys follow Settings > Remotes > TV volume */
    HUB_KEY_VOLUME_DOWN,
    HUB_KEY_MUTE,
    HUB_KEY_POWER,       /* the Streamer on or off (and the TV with it, over HDMI-CEC) */
    HUB_KEY_SLEEP,       /* the Streamer off; nothing if it is off already */
    HUB_KEY_WAKEUP,      /* the Streamer on; nothing if it is on already */
} hub_key_t;

/* Presses a key on the Google TV (the Streamer). */
esp_err_t hub_tv_key(hub_key_t key);

/* Sends a key of the TV set up over IR, by name: power_off, vol_up, hdmi1...
 * (docs/ir-tvs.md). Nothing happens without an IR TV or that key. */
esp_err_t hub_ir_tv_key(const char *key);

/* Turns the TV off. ir false: the Streamer goes to sleep, and the TV with it
 * over HDMI-CEC. ir true: the IR TV's power_off key, or its power key if it
 * has no separate one (which turns a TV that is already off on); without an
 * IR TV, the Streamer as above. */
esp_err_t hub_tv_off(bool ir);

/* Opens an app on the TV: a package name ("com.netflix.ninja", through the
 * companion app) or a deep link ("https://www.youtube.com"). */
esp_err_t hub_tv_open(const char *app);

/* --- Messages -------------------------------------------------------------- */

/* Tells the user something: a message on the remote and, when the module's
 * "Show messages on the TV" setting (HUB_SETTING_SHOW_ON_TV) is on, at the
 * top right of the TV for 10 seconds too (companion app 0.7+). When it
 * doesn't reach the TV, the remote's screen lights up to show it. icon is an
 * ICON_* string, or NULL. 160 characters at most. Any task. */
esp_err_t hub_notify(const hub_module_t *module, const char *icon, const char *text);

/* The setting hub_notify() reads: put it in a module's settings. */
#define HUB_SETTING_SHOW_ON_TV                                                                       \
    {                                                                                                \
        .key = "tv_msg", .label = "Show messages on the TV", .type = HUB_SETTING_SWITCH, .def = 1 \
    }

/* A message on the TV only (companion app 0.7+). */
esp_err_t hub_tv_notify(const char *text);

/* --- The TV --------------------------------------------------------------------- */

#define HUB_APP_ID_MAX   96
#define HUB_APP_NAME_MAX 48
#define HUB_TITLE_MAX    96

/* What the TV is playing, from the companion app (0.6+). */
typedef struct {
    char app[HUB_APP_ID_MAX];      /* the app's package */
    char title[HUB_TITLE_MAX];     /* "" if the app doesn't share it */
    char subtitle[HUB_TITLE_MAX];  /* episode, artist... */
    bool playing;                  /* false: paused */
} hub_now_playing_t;

/* False when nothing plays or there is no companion. HUB_EV_NOW_PLAYING
 * says when it changes. Any task. */
bool hub_now_playing(hub_now_playing_t *out);

/* Whether the TV is on, and the app in front ("" unknown), from the Google
 * TV itself: no companion needed. HUB_EV_TV says when either changes. When
 * the remote loses the Google TV for more than 30 s, the TV counts as off. */
typedef struct {
    bool on;
    char app[HUB_APP_ID_MAX];
} hub_tv_state_t;
void hub_tv_state(hub_tv_state_t *out);

/* The apps on the TV, from the companion app. */
typedef struct {
    char app[HUB_APP_ID_MAX];     /* package */
    char name[HUB_APP_NAME_MAX];
} hub_tv_app_t;

/* Copies up to max and returns how many (0 without the companion). Any task. */
size_t hub_tv_apps(hub_tv_app_t *out, size_t max);

/* --- The TV's internet speed (companion app 0.7+) ------------------------- */

typedef enum {
    HUB_SPEED_UNAVAILABLE = 0, /* no companion app 0.7+ on the TV right now */
    HUB_SPEED_IDLE,
    HUB_SPEED_LATENCY,
    HUB_SPEED_DOWNLOAD,
    HUB_SPEED_UPLOAD,
    HUB_SPEED_DONE,
    HUB_SPEED_FAILED,
} hub_speed_state_t;

typedef struct {
    hub_speed_state_t state;
    int progress;    /* 0-100 */
    float ping_ms;   /* the result, or the reading so far while it runs */
    float down_mbps;
    float up_mbps;
} hub_speedtest_t;

/* Starts an internet speed test on the TV, through the companion app:
 * latency, then download, then upload, about 15 s in all, against
 * Cloudflare's speed test. */
esp_err_t hub_tv_speedtest(void);
/* Its state; while it runs it changes about twice a second, so poll it
 * then (an lv_timer on your screen). Any task. */
void hub_speedtest(hub_speedtest_t *out);

/* Keeps the remote from turning itself off after idle hours (Settings >
 * Screen), for example while a timer runs; the screen still dims and goes
 * dark. Each hold needs a release. Any task. */
void hub_hold_awake(void);
void hub_release_awake(void);

/* --- For the platform (main/), not for modules ---------------------------- */

typedef enum {
    HUB_ACTION_KEY,
    HUB_ACTION_IR_TV_KEY,
    HUB_ACTION_TV_OFF,
    HUB_ACTION_OPEN,
    HUB_ACTION_NOTIFY,
    HUB_ACTION_SPEEDTEST,
    HUB_ACTION_UI_CALL, /* hub_ui_async() found LVGL's lock busy: make the call (hub_ui_call()) */
} hub_action_type_t;

typedef struct {
    hub_action_type_t type;
    hub_key_t key;  /* KEY */
    bool ir;        /* TV_OFF */
    char text[161]; /* IR_TV_KEY: the key's name; OPEN: the app or link; NOTIFY: the message */
    char icon[8];   /* NOTIFY: an ICON_* string, or "" */
    bool remote;    /* NOTIFY: show it on the remote */
    bool tv;        /* NOTIFY: show it on the TV */
    void (*fn)(void *arg); /* UI_CALL */
    void *arg;             /* UI_CALL */
} hub_action_t;

/* Queues an action for the controller; false if it couldn't. Any task. */
typedef bool (*hub_action_handler_t)(const hub_action_t *action);
void hub_set_action_handler(hub_action_handler_t handler);

/* HUB_ACTION_UI_CALL, on the controller task: hands the call to the LVGL
 * task, waiting for LVGL's lock as long as it takes. */
void hub_ui_call(const hub_action_t *action);

/* The companion's news, from the controller task: what is playing (NULL:
 * nothing), which raises HUB_EV_NOW_PLAYING, and the app list. */
void hub_set_now_playing(const hub_now_playing_t *now);
void hub_set_tv_apps(const hub_tv_app_t *apps, size_t count);
/* The TV's power and the app in front, from the controller task; a change
 * raises HUB_EV_TV. */
void hub_set_tv_power(bool on);
void hub_set_tv_app(const char *app);
/* The speed test's state, from the companion. */
void hub_set_speedtest(const hub_speedtest_t *speed);

/* True while a module holds the remote awake. */
bool hub_awake_held(void);

#ifdef __cplusplus
}
#endif
