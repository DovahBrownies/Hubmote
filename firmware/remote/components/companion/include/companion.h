#pragma once

/*
 * Client for the companion app on the Streamer (companion/ in the repo),
 * which serves HTTP on port 6470 of the TV. One task keeps its /events
 * stream open and, whenever it connects (and every 30 min), fetches the app
 * list, downloading icons only for apps that are new or updated.
 *
 * Companion 0.4 and later only answer a paired remote: the TV shows a code
 * (companion_pair_start()), the user types it (companion_pair()), and the
 * token the companion hands back is kept in NVS (namespace "companion") and
 * sent with every request.
 *
 * Companion 0.5 and later update themselves when asked (companion_update()):
 * the companion downloads the APK, and Android installs it, maybe after a
 * confirmation on the TV.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMPANION_PKG_MAX    96
#define COMPANION_LABEL_MAX  48
#define COMPANION_MAX_APPS   48
#define COMPANION_ICON       112 /* home tile, px, unless companion_set_icon_sizes() */
#define COMPANION_ICON_SMALL 44  /* app panel header, px, likewise */
#define COMPANION_VERSION    16
#define COMPANION_TITLE_MAX  96

typedef enum {
    COMPANION_FIELD_NONE = 0, /* nothing has input focus */
    COMPANION_FIELD_TEXT,
    COMPANION_FIELD_PASSWORD,
    COMPANION_FIELD_NUMBER,
} companion_field_t;

typedef struct {
    char pkg[COMPANION_PKG_MAX];
    char label[COMPANION_LABEL_MAX];
    int64_t updated;           /* the app's last update, ms; changes when it updates */
    const uint8_t *icon;       /* LVGL RGB565A8, the big size square, or NULL */
    const uint8_t *icon_small; /* LVGL RGB565A8, the small size square, or NULL */
} companion_app_t;

/* An update the companion is running (companion_update()). */
typedef enum {
    COMPANION_UPDATE_NONE = 0,
    COMPANION_UPDATE_DOWNLOADING,
    COMPANION_UPDATE_INSTALLING,
    COMPANION_UPDATE_CONFIRM, /* Android's install dialog is up on the TV */
    COMPANION_UPDATE_FAILED,
} companion_update_t;

/* What is playing on the TV (companion 0.6+, from Android's media sessions). */
typedef enum {
    COMPANION_MEDIA_NONE = 0,
    COMPANION_MEDIA_PLAYING,
    COMPANION_MEDIA_PAUSED,
    COMPANION_MEDIA_BUFFERING,
    COMPANION_MEDIA_STOPPED,
} companion_media_state_t;

typedef struct {
    char pkg[COMPANION_PKG_MAX];       /* the app playing; "" if nothing */
    char title[COMPANION_TITLE_MAX];   /* "" if the app doesn't share it */
    char subtitle[COMPANION_TITLE_MAX]; /* episode, artist... */
    companion_media_state_t state;
} companion_media_t;

typedef enum {
    COMPANION_EVT_ONLINE, /* .online changed; going offline also means the field is unknown */
    COMPANION_EVT_APPS,   /* the app list changed: read it with companion_apps() */
    COMPANION_EVT_FIELD,  /* .field or .field_id changed */
    COMPANION_EVT_PAIRING, /* .needs_pairing changed */
    COMPANION_EVT_UPDATE, /* .update changed */
    COMPANION_EVT_MEDIA,  /* what is playing changed: read it with companion_media() */
} companion_evt_type_t;

typedef struct {
    companion_evt_type_t type;
    bool online;
    bool needs_pairing; /* the companion turns this remote away until it pairs */
    companion_field_t field;
    int field_id; /* changes whenever a different field gets focus */
    /* The companion's version while online: its Android version code (0 =
     * unknown) and name ("0.5"), and whether it can update itself (0.5+). */
    int version;
    char version_name[COMPANION_VERSION];
    bool can_update;
    companion_update_t update;
} companion_event_t;

/* Runs on the companion task: copy and queue, don't block. */
typedef void (*companion_cb_t)(const companion_event_t *evt, void *ctx);

esp_err_t companion_start(companion_cb_t cb, void *ctx);
/* The icons' sizes in px (the UI's, which scale with the screen); before
 * companion_start(). Default COMPANION_ICON and COMPANION_ICON_SMALL. */
void companion_set_icon_sizes(int big, int small);

/* The TV's address. A change drops the current connection; "" stops. */
void companion_set_host(const char *host);

/* What is playing; false (and *out cleared) if the companion doesn't know
 * (offline, older than 0.6, or without notification access). */
bool companion_media(companion_media_t *out);

/* Copies up to max apps of the current list (sorted by label) into out and
 * returns how many. *gen identifies the list for companion_apps_applied(). */
size_t companion_apps(companion_app_t *out, size_t max, uint32_t *gen);

/* Call once nothing uses icons of lists older than gen any more (the UI
 * shows list gen): frees icons that only older lists had. */
void companion_apps_applied(uint32_t gen);

/* Keeps the icons in dir (created if missing; on a mounted filesystem), so
 * they don't have to be downloaded again after a restart or waking from
 * deep sleep. The files there are kept under max_bytes, and only for apps
 * still on the TV. Call once, before the companion connects. */
void companion_set_cache_dir(const char *dir, size_t max_bytes);

/* Pairing (companion 0.4+). Each blocks for one short HTTP request.
 * companion_pair_start(): the TV shows a 6-character code for 2 minutes (the
 * one already showing, if any). ESP_ERR_NOT_FINISHED: no code is showing and
 * the last one went up under 5 s ago (used or cancelled): try again shortly.
 * ESP_ERR_NOT_ALLOWED: locked after too many wrong codes (0.6+), *locked_s
 * (may be NULL) is the wait in seconds.
 * companion_pair(): the typed code. ESP_OK: paired, the token is saved and
 * the companion connects; ESP_ERR_INVALID_ARG: wrong code;
 * ESP_ERR_TIMEOUT: no code on the TV any more (expired, or 3 wrong tries):
 * start again. companion_pair_cancel(): takes the code off the TV. */
esp_err_t companion_pair_start(uint32_t *locked_s);
esp_err_t companion_pair(const char *code);
void companion_pair_cancel(void);

/* Opens the app on the TV. Blocks for one short HTTP request. */
esp_err_t companion_launch(const char *pkg);

/* Replaces the focused field's text (works where the TV's input method
 * doesn't, e.g. browsers), and the field's Enter action. Both block for one
 * short HTTP request. */
esp_err_t companion_set_text(const char *text);
esp_err_t companion_enter(void);
/* Opens Google TV's own search with this text (companion 0.4+). Blocks for
 * one short HTTP request. */
esp_err_t companion_search(const char *text);
/* Hides (or allows again) the TV's on-screen keyboard. The companion
 * allows it again by itself when the remote disconnects. */
esp_err_t companion_hide_keyboard(bool hidden);
/* Asks the companion (0.5+) to update itself from the APK at url (size
 * bytes, that SHA-256 in hex). Progress comes as COMPANION_EVT_UPDATE; the
 * new version shows when it is back online. Blocks for one short HTTP
 * request. */
esp_err_t companion_update(const char *url, int size, const char *sha256);

#ifdef __cplusplus
}
#endif
