#include "net.h"

#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "kv.h"
#include "hm_secrets.h" /* generated from secrets.env; WIFI_* may be empty */

static const char *TAG = "net";

#define NVS_NS    "wifi"
#define NVS_SSID  "ssid"
#define NVS_PASS  "pass"

static net_cb_t s_cb;
static void *s_ctx;
static volatile bool s_up;
static bool s_started;
static bool s_power_save = true; /* the mode wanted, applied once the radio starts */
static char s_ssid[33];
static char s_pass[65];
static bool s_unsaved;   /* s_ssid/s_pass aren't in NVS yet: save them once up */
static bool s_scanning;
static bool s_switching; /* net_join() is changing the settings: don't reconnect yet */
static net_scan_cb_t s_scan_cb;
static void *s_scan_ctx;

static void load(void)
{
    if (kv_get_str(NVS_NS, NVS_SSID, s_ssid, sizeof(s_ssid)) == ESP_OK &&
        kv_get_str(NVS_NS, NVS_PASS, s_pass, sizeof(s_pass)) == ESP_OK && s_ssid[0]) {
        return;
    }
    /* Nothing saved: a developer build may carry them in secrets.env. */
    strlcpy(s_ssid, HM_WIFI_SSID, sizeof(s_ssid));
    strlcpy(s_pass, HM_WIFI_PASSWORD, sizeof(s_pass));
    s_unsaved = s_ssid[0] != '\0';
    if (strlen(HM_WIFI_SSID) > 32) {
        ESP_LOGE(TAG, "WIFI_SSID is longer than 32 bytes; Wi-Fi names can't be");
    }
}

static void save(void)
{
    esp_err_t err = kv_set_str(NVS_NS, NVS_SSID, s_ssid);
    if (err == ESP_OK) {
        err = kv_set_str(NVS_NS, NVS_PASS, s_pass);
    }
    ESP_LOGI(TAG, "network saved: %s", esp_err_to_name(err));
    s_unsaved = false;
}

static esp_err_t apply(void)
{
    wifi_config_t wc = {0};
    /* Not strlcpy: a 32-byte name or a 64-character key fills the field
     * completely and must not lose its last character to a terminator. */
    memcpy(wc.sta.ssid, s_ssid, strnlen(s_ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, s_pass, strnlen(s_pass, sizeof(wc.sta.password)));
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    /* Accept WPA3-only and mixed WPA2/WPA3 routers. */
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set config: %s", esp_err_to_name(err));
    }
    return err;
}

static void connect(void)
{
    if (s_ssid[0] && !s_scanning && !s_switching) {
        esp_wifi_connect();
    }
}

static int by_rssi(const void *a, const void *b)
{
    return ((const net_ap_t *)b)->rssi - ((const net_ap_t *)a)->rssi;
}

static void scan_done(void)
{
    uint16_t n = NET_SCAN_MAX;
    wifi_ap_record_t *recs = heap_caps_calloc(NET_SCAN_MAX, sizeof(*recs), MALLOC_CAP_SPIRAM);
    net_ap_t *aps = heap_caps_calloc(NET_SCAN_MAX, sizeof(*aps), MALLOC_CAP_SPIRAM);
    size_t count = 0;
    if (recs && aps && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
        for (uint16_t i = 0; i < n; i++) {
            const char *ssid = (const char *)recs[i].ssid;
            if (!ssid[0]) {
                continue; /* hidden network: "Other network" covers those */
            }
            size_t j = 0;
            while (j < count && strcmp(aps[j].ssid, ssid) != 0) {
                j++;
            }
            if (j == count) {
                strlcpy(aps[count].ssid, ssid, sizeof(aps[count].ssid));
                aps[count].rssi = recs[i].rssi;
                aps[count].secure = recs[i].authmode != WIFI_AUTH_OPEN;
                count++;
            } else if (recs[i].rssi > aps[j].rssi) {
                aps[j].rssi = recs[i].rssi; /* one entry per name: the strongest */
            }
        }
        qsort(aps, count, sizeof(*aps), by_rssi);
    } else {
        esp_wifi_clear_ap_list();
    }
    ESP_LOGI(TAG, "scan: %u networks", (unsigned)count);
    s_scanning = false;
    net_scan_cb_t cb = s_scan_cb;
    s_scan_cb = NULL;
    if (cb) {
        cb(aps, aps ? count : 0, s_scan_ctx);
    }
    free(recs);
    free(aps);
    if (!s_up) {
        connect();
    }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        ESP_LOGW(TAG, "disconnected, reason %d", ev->reason);
        s_up = false;
        if (s_cb) {
            s_cb(false, ev->reason, s_ctx);
        }
        connect();
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        scan_done();
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *ev = data;
    ESP_LOGI(TAG, "up: " IPSTR, IP2STR(&ev->ip_info.ip));
    s_up = true;
    if (s_unsaved) {
        save();
    }
    if (s_cb) {
        s_cb(true, 0, s_ctx);
    }
}

void net_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* The driver's info lines include the network name; keep it out of logs,
     * which can leave the device over Wi-Fi (netlog.c). */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL));
    load();
}

bool net_configured(void)
{
    return s_ssid[0] != '\0';
}

const char *net_ssid(void)
{
    return s_ssid;
}

void net_start(net_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
    if (s_started) {
        return;
    }
    s_started = true;
    ESP_ERROR_CHECK(apply());
    /* Modem sleep while the screen is dark (net_set_power_save()): the
     * radio naps between beacons; the TV's 5 s pings still get through. */
    esp_wifi_set_ps(s_power_save ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_wifi_start());
}

void net_set_power_save(bool on)
{
    if (on == s_power_save) {
        return;
    }
    s_power_save = on;
    if (s_started) {
        esp_err_t err = esp_wifi_set_ps(on ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
        ESP_LOGI(TAG, "power save %s: %s", on ? "on" : "off", esp_err_to_name(err));
    }
}

esp_err_t net_join(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || (password && strlen(password) > 64)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_switching = true;
    esp_wifi_disconnect();
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, password ? password : "", sizeof(s_pass));
    s_unsaved = true;
    ESP_LOGI(TAG, "joining another network");
    esp_err_t err = apply();
    s_switching = false;
    connect();
    return err;
}

esp_err_t net_scan(net_scan_cb_t cb, void *ctx)
{
    if (s_scanning) {
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_cb = cb;
    s_scan_ctx = ctx;
    s_scanning = true;
    /* A scan isn't allowed while the station is connecting: pause that. */
    if (!s_up) {
        esp_wifi_disconnect();
    }
    wifi_scan_config_t sc = {.show_hidden = false};
    esp_err_t err = esp_wifi_scan_start(&sc, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
        s_scanning = false;
        s_scan_cb = NULL;
        connect();
    }
    return err;
}

void net_mdns_start(void)
{
    esp_err_t err = mdns_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        mdns_hostname_set(NET_HOSTNAME);
        mdns_instance_name_set("Hubmote");
    }
}

void net_hostname(char *out, size_t size)
{
    char name[64]; /* MDNS_NAME_BUF_LEN */
    if (size == 0) {
        return;
    }
    if (mdns_hostname_get(name) == ESP_OK) {
        strlcpy(out, name, size);
    } else {
        out[0] = '\0';
    }
}
