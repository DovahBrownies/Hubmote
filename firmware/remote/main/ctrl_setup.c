/*
 * The first-run flow and staying connected to the TV: the pairing identity,
 * Wi-Fi (the network picker when there is none), finding the TV (mDNS, or
 * the address in menuconfig), pairing, and finding it again when its
 * address changes. Part of the controller (ctrl_internal.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "atv_remote.h"
#include "bootloader_random.h"
#include "companion.h"
#include "ctrl_internal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "net.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "ui.h"

static const char *TAG = "ctrl";

#define MAX_TVS       8
#define DISCOVER_MS   3000
#define CONNECT_TRIES 3
#define RUNNING_TRIES 5 /* about 30 s of retries before checking if the TV moved */
#define WRONG_TV_TRIES 3 /* connections to a host with another key before saying so */
#define WIFI_TRIES    3
#define NVS_TV_NAME   "tv_mdns"

typedef enum {
    PH_START,
    PH_WIFI_SETUP, /* no network yet, or joining failed: the picker is up */
    PH_WIFI,       /* waiting for an IP */
    PH_CHOOSE,     /* showing the list of TVs */
    PH_CONNECTING, /* first connection to a host since it was chosen */
    PH_PAIRING,
    PH_RUNNING,    /* connected at least once; atv_remote reconnects by itself */
    PH_ERROR,
} phase_t;

/* What "Try again" on the error screen does. */
typedef enum { RETRY_NONE, RETRY_DISCOVER, RETRY_PAIR, RETRY_IDENTITY } retry_t;

static phase_t s_phase = PH_START;
static retry_t s_retry;
static atv_device_t s_found[MAX_TVS];
static size_t s_found_count;
static char s_tv_name[64] = "your TV";
static char s_mdns_name[64]; /* the chosen TV's mDNS name, kept in NVS */
static int s_connect_failures;
static int s_wifi_failures;
static bool s_moved; /* connecting to an address relocate_tv() found, not one the user chose */
static char s_wrong_hosts[4][40]; /* answered with another key than the paired TV's */
static size_t s_wrong_next;
static int s_wrong_tv; /* WRONG_TV answers since the last connection */
static net_cb_t s_on_net;

/* --- The TV's name in NVS -------------------------------------------------------------- */

void ctrl_setup_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_mdns_name);
    if (nvs_open(CTRL_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, NVS_TV_NAME, s_mdns_name, &len) != ESP_OK) {
            s_mdns_name[0] = '\0';
        }
        nvs_close(h);
    }
}

static void save_tv_name(const char *name)
{
    strlcpy(s_mdns_name, name, sizeof(s_mdns_name));
    nvs_handle_t h;
    if (nvs_open(CTRL_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_str(h, NVS_TV_NAME, s_mdns_name) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

/* --- Flow ------------------------------------------------------------------- */

static void show_error(const char *title, const char *body, retry_t retry)
{
    s_phase = PH_ERROR;
    s_retry = retry;
    ui_setup_error(title, body);
}

static void discover(void)
{
    s_phase = PH_CHOOSE;
    ui_setup_busy("Looking for your Google TV", "Make sure it is on and on the same Wi-Fi network.");
    size_t n = 0;
    esp_err_t err = atv_discover(s_found, MAX_TVS, &n, DISCOVER_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "discovery failed: %s", esp_err_to_name(err));
        n = 0;
    }
    s_found_count = n;
    ui_tv_t list[MAX_TVS];
    for (size_t i = 0; i < n; i++) {
        strlcpy(list[i].name, s_found[i].name, sizeof(list[i].name));
        strlcpy(list[i].host, s_found[i].host, sizeof(list[i].host));
    }
    ui_setup_tvs(list, n);
}

static void connect_to(const char *host)
{
    s_phase = PH_CONNECTING;
    s_connect_failures = 0;
    ui_setup_busy("Connecting to your TV", host);
    atv_connect();
}

static int wrong_host_slot(const char *host)
{
    for (size_t i = 0; i < sizeof(s_wrong_hosts) / sizeof(s_wrong_hosts[0]); i++) {
        if (s_wrong_hosts[i][0] && strcmp(s_wrong_hosts[i], host) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool is_wrong_host(const char *host)
{
    return wrong_host_slot(host) >= 0;
}

/* The TV may have been given a new IP address. Looks for it by its mDNS
 * name (with no name stored: the only TV on the network) and connects to
 * its new address. False when it isn't found anywhere new. Anyone can answer
 * with the TV's name: atv_remote checks the key, and addresses that failed
 * that check are skipped here. */
static bool relocate_tv(void)
{
    char host[40] = "";
    atv_get_host(host, sizeof(host));
    size_t n = 0;
    if (atv_discover(s_found, MAX_TVS, &n, DISCOVER_MS) != ESP_OK) {
        return false;
    }
    s_found_count = n;
    const atv_device_t *tv = NULL;
    for (size_t i = 0; i < n && !tv; i++) {
        if (!is_wrong_host(s_found[i].host) && (s_mdns_name[0] ? strcmp(s_found[i].name, s_mdns_name) == 0 : n == 1)) {
            tv = &s_found[i];
        }
    }
    if (!tv || strcmp(tv->host, host) == 0) {
        return false;
    }
    ESP_LOGI(TAG, "\"%s\" moved from %s to %s", tv->name, host, tv->host);
    save_tv_name(tv->name);
    atv_set_host(tv->host);
    connect_to(tv->host);
    s_moved = true;
    return true;
}

static void find_tv(void)
{
    char host[40];
    if (atv_get_host(host, sizeof(host))) {
        connect_to(host);
    } else if (CONFIG_HM_TV_HOST[0] != '\0' && atv_set_host(CONFIG_HM_TV_HOST) == ESP_OK) {
        connect_to(CONFIG_HM_TV_HOST);
    } else {
        discover();
    }
}

static void start_pairing(void)
{
    s_phase = PH_PAIRING;
    ui_setup_busy("Pairing with your TV", "Your TV will show a 6-character code.");
    atv_pair_start();
}

static void make_identity(void)
{
    if (atv_has_identity()) {
        return;
    }
    ui_setup_busy("Preparing secure pairing",
                  "Creating this remote's encryption key. This happens once and can take up to a minute.");
    int64_t t0 = esp_timer_get_time();
    /* The radio isn't on yet, and without it the RNG isn't truly random:
     * the internal entropy source stands in while the key is made (no ADC
     * is in use; the IO expander reads the battery). */
    bootloader_random_enable();
    esp_err_t err = atv_generate_identity();
    bootloader_random_disable();
    ESP_LOGI(TAG, "identity: %s in %lld ms", esp_err_to_name(err), (esp_timer_get_time() - t0) / 1000);
    if (err != ESP_OK) {
        show_error("Couldn't create the pairing key", esp_err_to_name(err), RETRY_IDENTITY);
    }
}

static void start_wifi(void)
{
    ui_set_wifi_network(net_ssid());
    if (!net_configured()) {
        /* First run: the radio starts for scanning; the user picks a network. */
        s_phase = PH_WIFI_SETUP;
        net_start(s_on_net, NULL);
        ui_setup_wifi(NULL);
        return;
    }
    s_phase = PH_WIFI;
    ui_setup_busy("Connecting to Wi-Fi", net_ssid());
    net_start(s_on_net, NULL);
}

void ctrl_setup_start(net_cb_t on_net)
{
    s_on_net = on_net;
    make_identity();
    if (s_phase != PH_ERROR) {
        start_wifi();
    }
}

/* Runs on the event task; the ui_* calls are thread-safe. */
static void on_scan(const net_ap_t *aps, size_t count, void *ctx)
{
    ui_wifi_ap_t *list = heap_caps_calloc(count ? count : 1, sizeof(*list), MALLOC_CAP_SPIRAM);
    if (!list) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        strlcpy(list[i].ssid, aps[i].ssid, sizeof(list[i].ssid));
        list[i].rssi = aps[i].rssi;
        list[i].secure = aps[i].secure;
    }
    ui_set_wifi_networks(list, count);
    free(list);
}

/* --- The TV's events ------------------------------------------------------------------- */

static void handle_atv_state(const atv_event_t *e)
{
    switch (e->state) {
    case ATV_STATE_CONNECTING:
        ui_set_link(UI_LINK_CONNECTING);
        break;
    case ATV_STATE_CONNECTED: {
        s_phase = PH_RUNNING;
        s_connect_failures = 0;
        s_moved = false;
        s_wrong_tv = 0;
        ui_set_link(UI_LINK_CONNECTED);
        ui_show_main();
        ctrl_screen_connected();
        controller_log_heap("connected");
        char host[40];
        if (atv_get_host(host, sizeof(host))) {
            int slot = wrong_host_slot(host); /* the paired TV is back there */
            if (slot >= 0) {
                s_wrong_hosts[slot][0] = '\0';
            }
            /* Only now, with the TV's key checked, does the companion get
             * the address (and with it the token). */
            companion_set_host(host);
        }
        break;
    }
    case ATV_STATE_NOT_PAIRED:
        companion_set_host(NULL);
        if (s_moved || atv_tv_key_pinned()) {
            /* Paired before, or found under the TV's name after a move: it
             * may not be the TV, so the user decides whether to pair. */
            s_moved = false;
            show_error("Pair with your TV again", "Your TV doesn't know this remote any more.", RETRY_PAIR);
        } else {
            start_pairing();
        }
        break;
    case ATV_STATE_PAIRING_CODE:
        ui_setup_code(s_tv_name);
        break;
    case ATV_STATE_DISCONNECTED:
        ui_set_link(UI_LINK_CONNECTING);
        companion_set_host(NULL); /* back once the paired TV is (CONNECTED) */
        if (e->error == ATV_ERR_WRONG_TV) {
            /* Not the paired TV: look for it elsewhere, once per address
             * (discovery blocks this task for seconds), and after a few
             * tries say so; atv_remote keeps retrying meanwhile. */
            char host[40];
            if (atv_get_host(host, sizeof(host)) && !is_wrong_host(host)) {
                strlcpy(s_wrong_hosts[s_wrong_next], host, sizeof(s_wrong_hosts[0]));
                s_wrong_next = (s_wrong_next + 1) % (sizeof(s_wrong_hosts) / sizeof(s_wrong_hosts[0]));
                if ((s_phase == PH_RUNNING || s_phase == PH_CONNECTING) && relocate_tv()) {
                    break;
                }
            }
            if (++s_wrong_tv >= WRONG_TV_TRIES) {
                s_wrong_tv = 0;
                atv_disconnect();
                show_error("Not your TV",
                           "Something else answers at your TV's address. Try again looks for TVs; pick yours, "
                           "and pair again if it was reset.",
                           RETRY_DISCOVER);
            }
            break;
        }
        if (s_phase == PH_CONNECTING && ++s_connect_failures >= CONNECT_TRIES) {
            atv_disconnect();
            if (!relocate_tv()) {
                show_error("Can't reach your TV",
                           "It may be off, or its address may have changed. Try again searches the network.",
                           RETRY_DISCOVER);
            }
        } else if (s_phase == PH_RUNNING && ++s_connect_failures >= RUNNING_TRIES) {
            /* Keep retrying, but check now and then whether it moved. */
            s_connect_failures = 0;
            atv_disconnect();
            if (!relocate_tv()) {
                atv_connect();
            }
        }
        break;
    case ATV_STATE_IDLE:
    case ATV_STATE_PAIRING:
        break;
    }
}

void ctrl_setup_atv(const atv_event_t *e)
{
    switch (e->type) {
    case ATV_EVT_STATE:
        handle_atv_state(e);
        break;
    case ATV_EVT_PAIR_RESULT:
        if (e->error == ESP_OK) {
            s_phase = PH_CONNECTING;
            s_connect_failures = 0;
            ui_setup_busy("Paired", "Connecting to your TV");
        } else if (e->error == ESP_ERR_INVALID_ARG) {
            ui_setup_code_wrong();
        } else {
            show_error("Pairing didn't finish", "Try again to show a new code on the TV.", RETRY_PAIR);
        }
        break;
    case ATV_EVT_DEVICE_INFO:
        if (e->device.model[0]) {
            strlcpy(s_tv_name, e->device.model, sizeof(s_tv_name));
            ui_set_tv_name(s_tv_name);
        }
        break;
    default:
        break;
    }
}

/* --- Wi-Fi ------------------------------------------------------------------------------- */

/* Joining keeps failing during setup: back to the network list, saying why.
 * The radio keeps trying the network meanwhile; if it gets through, setup
 * carries on by itself. */
static void show_wifi_error(int reason)
{
    static char msg[160];
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        snprintf(msg, sizeof(msg), "Couldn't find \"%s\". The remote only sees 2.4 GHz networks.", net_ssid());
        break;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        snprintf(msg, sizeof(msg), "\"%s\" didn't accept the password.", net_ssid());
        break;
    default:
        snprintf(msg, sizeof(msg), "Couldn't join \"%s\" (reason %d).", net_ssid(), reason);
        break;
    }
    s_phase = PH_WIFI_SETUP;
    ui_setup_wifi(msg);
}

void ctrl_setup_net(bool up, int reason)
{
    if (up) {
        s_wifi_failures = 0;
        if (s_phase == PH_WIFI || s_phase == PH_WIFI_SETUP) {
            find_tv();
        }
    } else if (s_phase == PH_WIFI && ++s_wifi_failures >= WIFI_TRIES) {
        show_wifi_error(reason);
    }
}

/* --- UI actions --------------------------------------------------------------------------- */

bool ctrl_setup_ui(const ctrl_ui_t *a)
{
    switch (a->type) {
    case UI_ACT_PAIR_CODE:
        atv_pair_finish(a->text);
        return true;
    case UI_ACT_PAIR_RESTART:
        atv_pair_cancel();
        start_pairing();
        return true;
    case UI_ACT_PICK_TV:
        if (a->index >= 0 && (size_t)a->index < s_found_count) {
            const atv_device_t *tv = &s_found[a->index];
            strlcpy(s_tv_name, tv->name, sizeof(s_tv_name));
            ui_set_tv_name(s_tv_name);
            save_tv_name(tv->name);
            atv_forget_tv_key(); /* the user's choice: pinned on connect */
            memset(s_wrong_hosts, 0, sizeof(s_wrong_hosts));
            s_moved = false;
            atv_set_host(tv->host);
            connect_to(tv->host);
        }
        return true;
    case UI_ACT_RESCAN:
        atv_disconnect();
        discover();
        return true;
    case UI_ACT_REPAIR:
        atv_disconnect();
        start_pairing();
        return true;
    case UI_ACT_WIFI_SCAN:
        if (net_scan(on_scan, NULL) != ESP_OK) {
            ui_set_wifi_networks(NULL, 0);
        }
        return true;
    case UI_ACT_WIFI_JOIN:
        if (net_join(a->name, a->text) != ESP_OK) {
            return true;
        }
        ui_set_wifi_network(a->name);
        if (s_phase != PH_RUNNING) { /* still setting up: show progress there */
            s_phase = PH_WIFI;
            s_wifi_failures = 0;
            ui_setup_busy("Connecting to Wi-Fi", a->name);
        }
        return true;
    case UI_ACT_RETRY:
        switch (s_retry) {
        case RETRY_DISCOVER:
            discover();
            break;
        case RETRY_PAIR:
            start_pairing();
            break;
        case RETRY_IDENTITY:
            make_identity();
            if (atv_has_identity()) {
                start_wifi();
            }
            break;
        case RETRY_NONE:
            break;
        }
        return true;
    default:
        return false;
    }
}
