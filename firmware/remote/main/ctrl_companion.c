/*
 * The companion app on the TV and typing: its app list on Home, pairing
 * with it, updating it (Settings > Remotes), and where typed text goes (the
 * companion's field, Google TV's search on Home, or the remote protocol).
 * Part of the controller (ctrl_internal.h).
 *
 * Requests to the companion run on a worker task of their own (see
 * "Requests" below), so the controller never waits for the network.
 */
#include <string.h>
#include "atv_remote.h"
#include "companion.h"
#include "ctrl_internal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hub.h"
#include "kv.h"
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

static void load_media_apps(void);

/* --- Requests ---------------------------------------------------------------------------------- */

/* Each request is an HTTP round trip that a slow device, or a wrong one at
 * the TV's address, can stretch to about 30 s. They run in order on this
 * worker, so the controller and the screen's reactions never wait for one.
 * The worker shows pairing results itself (the ui_* calls take the LVGL
 * lock) and reports a failed update request as a companion event, so the
 * controller's own state stays on the controller task. */
#define WORKER_STACK 5120 /* pairing (with its NVS write) peaked at about 3.2 KB */
#define WORKER_QUEUE 8

typedef enum {
    REQ_LAUNCH,
    REQ_TEXT,
    REQ_ENTER,
    REQ_SEARCH,
    REQ_HIDE_KEYBOARD,
    REQ_PAIR_START,
    REQ_PAIR,
    REQ_PAIR_CANCEL,
    REQ_UPDATE,
    REQ_NOTIFY,
} req_type_t;

typedef struct {
    req_type_t type;
    bool hidden;     /* REQ_HIDE_KEYBOARD */
    int size;        /* REQ_UPDATE: the APK's size and SHA-256; REQ_NOTIFY: seconds */
    char sha256[65];
    char text[161];  /* the package, search, code, APK URL or message */
} req_t;

static QueueHandle_t s_req_q;
static companion_cb_t s_on_event;
/* The field's latest text: typing sends it on every change, and a slow
 * companion only needs the newest, so REQ_TEXT sends whatever is here. */
static portMUX_TYPE s_text_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_text[sizeof(((ctrl_ui_t *)0)->text)];
static bool s_text_new;

static bool request(const req_t *r)
{
    if (!s_req_q || xQueueSend(s_req_q, r, 0) != pdTRUE) {
        ESP_LOGW(TAG, "companion busy, dropped request %d", r->type);
        return false;
    }
    return true;
}

static void request_text(req_type_t type, const char *text)
{
    req_t r = {.type = type};
    strlcpy(r.text, text, sizeof(r.text));
    request(&r);
}

static void run(const req_t *r)
{
    esp_err_t err = ESP_OK;
    switch (r->type) {
    case REQ_LAUNCH:
        err = companion_launch(r->text);
        ESP_LOGI(TAG, "launch %s: %s", r->text, esp_err_to_name(err));
        break;
    case REQ_TEXT: {
        char text[sizeof(s_text)];
        taskENTER_CRITICAL(&s_text_mux);
        bool fresh = s_text_new;
        s_text_new = false;
        memcpy(text, s_text, sizeof(text));
        taskEXIT_CRITICAL(&s_text_mux);
        if (fresh) {
            /* Works in every app, browsers included; the length only, as it
             * may be a password. */
            err = companion_set_text(text);
            ESP_LOGI(TAG, "text (%u chars) via companion: %s", (unsigned)strlen(text), esp_err_to_name(err));
        }
        break;
    }
    case REQ_ENTER:
        err = companion_enter();
        if (err != ESP_OK) {
            atv_send_key(ATV_KEY_ENTER, ATV_DIR_SHORT);
        }
        ESP_LOGI(TAG, "enter via %s", err == ESP_OK ? "companion" : "key");
        break;
    case REQ_SEARCH:
        err = companion_search(r->text);
        ESP_LOGI(TAG, "search Google TV (%u chars): %s", (unsigned)strlen(r->text), esp_err_to_name(err));
        break;
    case REQ_HIDE_KEYBOARD:
        companion_hide_keyboard(r->hidden);
        break;
    case REQ_PAIR_START: {
        uint32_t locked_s;
        err = companion_pair_start(&locked_s); /* sets locked_s first */
        if (err == ESP_ERR_NOT_FINISHED) {
            ui_notify(ICON_ALERT, "Try again in a few seconds");
        } else {
            ui_companion_pairing(err == ESP_OK, locked_s);
        }
        break;
    }
    case REQ_PAIR:
        err = companion_pair(r->text);
        ui_companion_pair_result(err == ESP_OK                ? UI_PAIR_OK
                                 : err == ESP_ERR_INVALID_ARG ? UI_PAIR_WRONG
                                 : err == ESP_ERR_TIMEOUT     ? UI_PAIR_EXPIRED
                                                              : UI_PAIR_FAILED);
        break;
    case REQ_PAIR_CANCEL:
        companion_pair_cancel();
        break;
    case REQ_NOTIFY:
        err = companion_notify(r->text, r->size);
        ESP_LOGI(TAG, "message on the TV: %s", esp_err_to_name(err));
        break;
    case REQ_UPDATE:
        err = companion_update(r->text, r->size, r->sha256);
        ESP_LOGI(TAG, "companion update request: %s", esp_err_to_name(err));
        if (err != ESP_OK && s_on_event) {
            /* As the companion reports a failed update; it was online when
             * the update was asked for. */
            companion_event_t e = {.type = COMPANION_EVT_UPDATE, .online = true, .update = COMPANION_UPDATE_FAILED};
            s_on_event(&e, NULL);
        }
        break;
    }
}

static void worker(void *arg)
{
    static bool stack_logged;
    req_t r;
    for (;;) {
        if (xQueueReceive(s_req_q, &r, portMAX_DELAY) == pdTRUE) {
            run(&r);
            if (!stack_logged && r.type == REQ_PAIR) {
                stack_logged = true;
                ESP_LOGI(TAG, "companion worker: stack left %u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
            }
        }
    }
}

void ctrl_companion_launch(const char *package)
{
    request_text(REQ_LAUNCH, package);
}

#define NOTIFY_VERSION 7 /* companion 0.7 shows messages (/notify) */
#define NOTIFY_SECONDS 10

bool ctrl_companion_notify(const char *text)
{
    if (!s_companion_online || s_comp_code < NOTIFY_VERSION) {
        return false;
    }
    req_t r = {.type = REQ_NOTIFY, .size = NOTIFY_SECONDS};
    strlcpy(r.text, text, sizeof(r.text));
    return request(&r);
}

void ctrl_companion_start(companion_cb_t on_event)
{
    s_on_event = on_event;
    s_req_q = xQueueCreateWithCaps(WORKER_QUEUE, sizeof(req_t), MALLOC_CAP_SPIRAM);
    if (!s_req_q || xTaskCreatePinnedToCore(worker, "comp_req", WORKER_STACK, NULL, 5, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "companion worker didn't start");
    }
    load_media_apps();
    companion_start(on_event, NULL);
}

/* --- Which apps get playback keys (ui_set_media_apps) ----------------------------------------- */

#define NVS_MEDIA_ON  "media_on"
#define NVS_MEDIA_OFF "media_off"

static void load_media_apps(void)
{
    char *on = heap_caps_calloc(2, UI_MEDIA_LIST_MAX, MALLOC_CAP_SPIRAM);
    if (!on) {
        return;
    }
    char *off = on + UI_MEDIA_LIST_MAX;
    kv_get_str(CTRL_NVS_NS, NVS_MEDIA_ON, on, UI_MEDIA_LIST_MAX);
    kv_get_str(CTRL_NVS_NS, NVS_MEDIA_OFF, off, UI_MEDIA_LIST_MAX);
    ui_set_media_apps(on, off);
    free(on);
}

static void save_media_apps(void)
{
    char *on = heap_caps_calloc(2, UI_MEDIA_LIST_MAX, MALLOC_CAP_SPIRAM);
    if (!on) {
        return;
    }
    char *off = on + UI_MEDIA_LIST_MAX;
    ui_media_apps(on, off);
    kv_set_str(CTRL_NVS_NS, NVS_MEDIA_ON, on);
    kv_set_str(CTRL_NVS_NS, NVS_MEDIA_OFF, off);
    free(on);
}

/* The app panel's "now playing" (companion 0.6+). An app that plays gets
 * playback keys from now on. */
static void show_media(void)
{
    companion_media_t m;
    companion_media(&m);
    ui_set_media(m.pkg, m.title, m.subtitle, m.state == COMPANION_MEDIA_PLAYING);
    hub_now_playing_t now = {.playing = m.state == COMPANION_MEDIA_PLAYING};
    strlcpy(now.app, m.pkg, sizeof(now.app));
    strlcpy(now.title, m.title, sizeof(now.title));
    strlcpy(now.subtitle, m.subtitle, sizeof(now.subtitle));
    hub_set_now_playing(m.pkg[0] && m.state != COMPANION_MEDIA_NONE ? &now : NULL);
    if (m.pkg[0] && m.state != COMPANION_MEDIA_NONE && ui_learn_media_app(m.pkg)) {
        ESP_LOGI(TAG, "playback keys for %s from now on", m.pkg);
        save_media_apps();
    }
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
    /* The same list for modules (hub_tv_apps) */
    static hub_tv_app_t *hub_list;
    if (!hub_list) {
        hub_list = heap_caps_calloc(COMPANION_MAX_APPS, sizeof(hub_tv_app_t), MALLOC_CAP_SPIRAM);
    }
    if (hub_list) {
        for (size_t i = 0; i < n; i++) {
            strlcpy(hub_list[i].app, apps[i].pkg, sizeof(hub_list[i].app));
            strlcpy(hub_list[i].name, apps[i].label, sizeof(hub_list[i].name));
        }
        hub_set_tv_apps(hub_list, n);
    }
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
    req_t r = {.type = REQ_UPDATE, .size = s_rel_comp.size};
    update_file_url(r.text, sizeof(r.text), s_rel_comp.apk);
    strlcpy(r.sha256, s_rel_comp.sha256, sizeof(r.sha256));
    ESP_LOGI(TAG, "companion update %s -> %s", s_comp_name, s_rel_comp.version);
    s_comp_target = s_rel_comp.code;
    s_comp_run = request(&r) ? UI_COMPANION_UPDATE_RUNNING : UI_COMPANION_UPDATE_FAILED;
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
            hub_set_tv_apps(NULL, 0);
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
    case UI_ACT_LAUNCH_APP:
        request_text(REQ_LAUNCH, a->text);
        return true;
    case UI_ACT_TEXT:
        if (s_companion_field) {
            taskENTER_CRITICAL(&s_text_mux);
            memcpy(s_text, a->text, sizeof(s_text));
            s_text_new = true;
            taskEXIT_CRITICAL(&s_text_mux);
            req_t r = {.type = REQ_TEXT};
            request(&r);
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
            req_t r = {.type = REQ_HIDE_KEYBOARD, .hidden = a->index != 0};
            request(&r);
        }
        return true;
    case UI_ACT_ENTER:
        if (s_companion_field) {
            req_t r = {.type = REQ_ENTER}; /* falls back to the key if the companion can't */
            request(&r);
        } else {
            atv_send_key(ATV_KEY_ENTER, ATV_DIR_SHORT);
            ESP_LOGI(TAG, "enter via key");
        }
        return true;
    case UI_ACT_COMPANION_PAIR: {
        req_t r = {.type = REQ_PAIR_START};
        request(&r);
        return true;
    }
    case UI_ACT_COMPANION_CODE:
        request_text(REQ_PAIR, a->text);
        return true;
    case UI_ACT_COMPANION_CANCEL: {
        req_t r = {.type = REQ_PAIR_CANCEL};
        request(&r);
        return true;
    }
    case UI_ACT_COMPANION_UPDATE:
        start_companion_update();
        return true;
    case UI_ACT_MEDIA_KEYS:
        save_media_apps();
        return true;
    default:
        return false;
    }
}

void ctrl_companion_tick(int64_t now)
{
    if (s_search_at && now >= s_search_at) {
        s_search_at = 0;
        request_text(REQ_SEARCH, s_search);
    }
}
