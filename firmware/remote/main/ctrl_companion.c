/*
 * The companion app on the TV and typing: its app list on Home, pairing
 * with it, updating it (Settings > Remotes), and where typed text goes (the
 * companion's field, Google TV's search on Home, or the remote protocol).
 * Part of the controller (ctrl_internal.h).
 */
#include <string.h>
#include "atv_remote.h"
#include "companion.h"
#include "ctrl_internal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui.h"
#include "ui_icons.h"
#include "update.h"

static const char *TAG = "ctrl";

/* Google TV search typed on Home: run once typing pauses for SEARCH_PAUSE_US
 * with at least SEARCH_MIN_CHARS, or at once on Enter. Shorter pauses and
 * texts flashed half-typed searches ("d" went to Gemini as a question). */
#define SEARCH_PAUSE_US  (1500 * 1000LL)
#define SEARCH_MIN_CHARS 3

static bool s_companion_field; /* the companion sees a focused field: type through it */
static bool s_companion_online;
/* Companion updates (Settings > Remotes): the release's companion (from
 * update.json) against the one on the TV (its /status). s_comp_run is the
 * update under way, if any; s_comp_target its version code. */
static update_companion_t s_rel_comp;
static int s_comp_code; /* on the TV; 0 = unknown */
static char s_comp_name[COMPANION_VERSION];
static bool s_comp_can_update;
static ui_companion_update_t s_comp_run;
static int s_comp_target;
static char s_search[128];
static int64_t s_search_at; /* when to run it; 0 = nothing waiting */

void ctrl_companion_start(companion_cb_t on_event)
{
    companion_start(on_event, NULL);
}

/* The app panel's "now playing" (companion 0.6+). */
static void show_media(void)
{
    companion_media_t m;
    companion_media(&m);
    ui_set_media(m.pkg, m.title, m.subtitle, m.state == COMPANION_MEDIA_PLAYING);
}

/* The companion app's list replaces the built-in apps while it is online. */
static void show_companion_apps(void)
{
    static companion_app_t *apps;
    if (!apps) {
        apps = heap_caps_calloc(COMPANION_MAX_APPS, sizeof(companion_app_t), MALLOC_CAP_SPIRAM);
        if (!apps) {
            return;
        }
    }
    uint32_t gen;
    size_t n = companion_apps(apps, COMPANION_MAX_APPS, &gen);
    ui_app_t list[COMPANION_MAX_APPS];
    for (size_t i = 0; i < n; i++) {
        list[i] = (ui_app_t){.pkg = apps[i].pkg, .label = apps[i].label, .icon = apps[i].icon,
                             .icon_small = apps[i].icon_small};
    }
    ui_set_apps(list, n);
    companion_apps_applied(gen);
}

/* --- Updating the companion ----------------------------------------------------------------- */

/* Settings > Remotes: the companion's version, and Update companion when
 * the release has a newer one. */
static void show_companion_update(void)
{
    ui_companion_update_t state = s_comp_run;
    if (state == UI_COMPANION_UPDATE_NONE && s_companion_online && s_comp_code > 0 && s_rel_comp.code > s_comp_code) {
        state = UI_COMPANION_UPDATE_AVAILABLE;
    }
    ui_set_companion_version(s_comp_name, state, s_rel_comp.version, s_comp_can_update && s_rel_comp.apk[0]);
}

static void start_companion_update(void)
{
    if (!s_companion_online || !s_comp_can_update || !s_rel_comp.apk[0] || s_rel_comp.code <= s_comp_code) {
        return;
    }
    char url[160];
    update_file_url(url, sizeof(url), s_rel_comp.apk);
    esp_err_t err = companion_update(url, s_rel_comp.size, s_rel_comp.sha256);
    ESP_LOGI(TAG, "companion update %s -> %s: %s", s_comp_name, s_rel_comp.version, esp_err_to_name(err));
    s_comp_target = s_rel_comp.code;
    s_comp_run = err == ESP_OK ? UI_COMPANION_UPDATE_RUNNING : UI_COMPANION_UPDATE_FAILED;
    show_companion_update();
}

void ctrl_companion_release(const update_companion_t *c)
{
    s_rel_comp = *c;
    show_companion_update();
}

/* --- The companion's events ------------------------------------------------------------------ */

void ctrl_companion_event(const companion_event_t *e)
{
    ui_set_companion(e->online ? UI_COMPANION_ON : e->needs_pairing ? UI_COMPANION_UNPAIRED : UI_COMPANION_NONE);
    switch (e->type) {
    case COMPANION_EVT_PAIRING:
        break;
    case COMPANION_EVT_MEDIA:
        show_media();
        break;
    case COMPANION_EVT_UPDATE: {
        static const ui_companion_update_t map[] = {
            [COMPANION_UPDATE_NONE] = UI_COMPANION_UPDATE_NONE,
            [COMPANION_UPDATE_DOWNLOADING] = UI_COMPANION_UPDATE_RUNNING,
            [COMPANION_UPDATE_INSTALLING] = UI_COMPANION_UPDATE_RUNNING,
            [COMPANION_UPDATE_CONFIRM] = UI_COMPANION_UPDATE_CONFIRM,
            [COMPANION_UPDATE_FAILED] = UI_COMPANION_UPDATE_FAILED,
        };
        s_comp_run = map[e->update];
        show_companion_update();
        break;
    }
    case COMPANION_EVT_ONLINE:
        s_companion_online = e->online;
        if (e->online) {
            s_comp_code = e->version;
            strlcpy(s_comp_name, e->version_name, sizeof(s_comp_name));
            s_comp_can_update = e->can_update;
            /* Back after an update: Android restarted it as the new version,
             * or it is still the old one and the update didn't happen. */
            if (s_comp_run != UI_COMPANION_UPDATE_NONE) {
                bool done = s_comp_code >= s_comp_target;
                if (s_comp_run != UI_COMPANION_UPDATE_FAILED) {
                    ESP_LOGI(TAG, "companion back as %s: %s", s_comp_name, done ? "updated" : "not updated");
                }
                s_comp_run = done ? UI_COMPANION_UPDATE_NONE : UI_COMPANION_UPDATE_FAILED;
            }
            if (!s_comp_can_update) {
                s_comp_run = UI_COMPANION_UPDATE_NONE; /* only the versions to show */
            }
            show_companion_apps();
        } else {
            s_companion_field = false;
            ui_set_apps(NULL, 0);
            ui_set_field(UI_FIELD_UNKNOWN, 0);
        }
        show_companion_update();
        show_media();
        break;
    case COMPANION_EVT_APPS:
        show_companion_apps();
        break;
    case COMPANION_EVT_FIELD: {
        static const ui_field_t map[] = {
            [COMPANION_FIELD_NONE] = UI_FIELD_NONE,
            [COMPANION_FIELD_TEXT] = UI_FIELD_TEXT,
            [COMPANION_FIELD_PASSWORD] = UI_FIELD_PASSWORD,
            [COMPANION_FIELD_NUMBER] = UI_FIELD_NUMBER,
        };
        s_companion_field = e->field != COMPANION_FIELD_NONE;
        ui_set_field(map[e->field], e->field_id);
        break;
    }
    }
}

/* --- UI actions -------------------------------------------------------------------------------- */

bool ctrl_companion_ui(const ctrl_ui_t *a)
{
    switch (a->type) {
    case UI_ACT_LAUNCH_APP: {
        esp_err_t err = companion_launch(a->text);
        ESP_LOGI(TAG, "launch %s: %s", a->text, esp_err_to_name(err));
        return true;
    }
    case UI_ACT_TEXT:
        if (s_companion_field) {
            /* Works in every app, browsers included; the length only, as it
             * may be a password. */
            esp_err_t err = companion_set_text(a->text);
            ESP_LOGI(TAG, "text (%u chars) via companion: %s", (unsigned)strlen(a->text), esp_err_to_name(err));
        } else if (a->text[0]) {
            /* The protocol needs at least one character; an empty field
             * can't be sent, so the TV keeps what it had. */
            atv_send_text(a->text);
        }
        return true;
    case UI_ACT_TYPE: {
        /* No field known: the TV inserts the text where its input goes
         * (Android turns it into virtual-keyboard keys for apps without a
         * text field). Lengths only in the log. */
        esp_err_t err = atv_send_text(a->text);
        ESP_LOGI(TAG, "type (%u chars): %s", (unsigned)strlen(a->text), esp_err_to_name(err));
        return true;
    }
    case UI_ACT_SEARCH:
        strlcpy(s_search, a->text, sizeof(s_search));
        if (a->index && s_search[0]) {
            s_search_at = esp_timer_get_time(); /* Enter */
        } else if (strlen(s_search) >= SEARCH_MIN_CHARS) {
            s_search_at = esp_timer_get_time() + SEARCH_PAUSE_US;
        } else {
            s_search_at = 0;
        }
        return true;
    case UI_ACT_KEYBOARD_SHOWN:
        /* The TV's own on-screen keyboard stays away while ours is open. */
        if (s_companion_online) {
            companion_hide_keyboard(a->index != 0);
        }
        return true;
    case UI_ACT_ENTER: {
        esp_err_t err = s_companion_field ? companion_enter() : ESP_ERR_NOT_SUPPORTED;
        if (err != ESP_OK) {
            atv_send_key(ATV_KEY_ENTER, ATV_DIR_SHORT);
        }
        ESP_LOGI(TAG, "enter via %s", err == ESP_OK ? "companion" : "key");
        return true;
    }
    case UI_ACT_COMPANION_PAIR: {
        uint32_t locked_s;
        esp_err_t err = companion_pair_start(&locked_s); /* sets locked_s first */
        if (err == ESP_ERR_NOT_FINISHED) {
            ui_notify(ICON_ALERT, "Try again in a few seconds");
        } else {
            ui_companion_pairing(err == ESP_OK, locked_s);
        }
        return true;
    }
    case UI_ACT_COMPANION_CODE: {
        esp_err_t err = companion_pair(a->text);
        ui_companion_pair_result(err == ESP_OK                ? UI_PAIR_OK
                                 : err == ESP_ERR_INVALID_ARG ? UI_PAIR_WRONG
                                 : err == ESP_ERR_TIMEOUT     ? UI_PAIR_EXPIRED
                                                              : UI_PAIR_FAILED);
        return true;
    }
    case UI_ACT_COMPANION_CANCEL:
        companion_pair_cancel();
        return true;
    case UI_ACT_COMPANION_UPDATE:
        start_companion_update();
        return true;
    default:
        return false;
    }
}

void ctrl_companion_tick(int64_t now)
{
    if (s_search_at && now >= s_search_at) {
        s_search_at = 0;
        esp_err_t err = companion_search(s_search);
        ESP_LOGI(TAG, "search Google TV (%u chars): %s", (unsigned)strlen(s_search), esp_err_to_name(err));
    }
}
