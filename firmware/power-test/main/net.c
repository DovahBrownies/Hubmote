#include "net.h"

#include <string.h>
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "ping/ping_sock.h"
#include "sdkconfig.h"

static const char *TAG = "net";

#define GOT_IP_BIT BIT0
#define AP_CACHE_MAGIC 0x41504341u /* "APCA" */

/* Last good AP, kept across esp_restart and deep sleep so a reconnect can
 * skip the scan (BSSID + channel). Invalidated if it stops working. */
typedef struct {
    uint32_t magic;
    uint8_t bssid[6];
    uint8_t channel;
} ap_cache_t;
RTC_NOINIT_ATTR static ap_cache_t s_ap_cache;

static esp_netif_t *s_netif;
static EventGroupHandle_t s_events;
static net_mode_t s_mode = NET_OFF;
static bool s_started;
static volatile bool s_connected;
static bool s_used_cache;
static int64_t s_start_time_us;
static int64_t s_ip_time_us;
static esp_ping_handle_t s_ping;

static void apply_config(void)
{
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, CONFIG_PT_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_PT_WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.scan_method = WIFI_FAST_SCAN;
    wc.sta.listen_interval = 3; /* only used by WIFI_PS_MAX_MODEM */
    s_used_cache = s_ap_cache.magic == AP_CACHE_MAGIC;
    if (s_used_cache) {
        memcpy(wc.sta.bssid, s_ap_cache.bssid, sizeof(wc.sta.bssid));
        wc.sta.bssid_set = true;
        wc.sta.channel = s_ap_cache.channel;
    }
    esp_wifi_set_config(WIFI_IF_STA, &wc);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_events, GOT_IP_BIT);
        if (s_mode == NET_OFF) {
            return;
        }
        if (s_used_cache) {
            ESP_LOGW(TAG, "cached AP failed, falling back to a full scan");
            s_ap_cache.magic = 0;
            apply_config();
        }
        esp_wifi_connect();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    s_ip_time_us = esp_timer_get_time();
    s_connected = true;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        memcpy(s_ap_cache.bssid, ap.bssid, sizeof(s_ap_cache.bssid));
        s_ap_cache.channel = ap.primary;
        s_ap_cache.magic = AP_CACHE_MAGIC;
    }
    const ip_event_got_ip_t *ev = data;
    ESP_LOGI(TAG, "got " IPSTR " at %lld ms, %lld ms after start (%s)", IP2STR(&ev->ip_info.ip),
             s_ip_time_us / 1000, (s_ip_time_us - s_start_time_us) / 1000,
             s_used_cache ? "fast connect" : "full scan");
    xEventGroupSetBits(s_events, GOT_IP_BIT);
}

void net_init(void)
{
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL));
}

bool net_available(void)
{
    return CONFIG_PT_WIFI_SSID[0] != '\0';
}

void net_set_mode(net_mode_t mode)
{
    if (mode != NET_OFF && !net_available()) {
        ESP_LOGW(TAG, "no SSID configured; staying off");
        mode = NET_OFF;
    }
    if (mode == s_mode) {
        return;
    }
    net_mode_t prev = s_mode;
    s_mode = mode;

    if (mode == NET_OFF) {
        net_keepalive_stop();
        if (s_started) {
            esp_wifi_stop();
            s_started = false;
        }
        s_connected = false;
        xEventGroupClearBits(s_events, GOT_IP_BIT);
        return;
    }

    static const wifi_ps_type_t ps[] = {
        [NET_ON_NO_PS] = WIFI_PS_NONE,
        [NET_ON_MIN_PS] = WIFI_PS_MIN_MODEM,
        [NET_ON_MAX_PS] = WIFI_PS_MAX_MODEM,
    };
    esp_wifi_set_ps(ps[mode]);
    if (!s_started) {
        apply_config();
        s_start_time_us = esp_timer_get_time();
        s_ip_time_us = 0;
        esp_wifi_start();
        s_started = true;
    } else if (prev != NET_ON_MAX_PS && mode == NET_ON_MAX_PS) {
        /* The listen interval is negotiated at association: reconnect so the
         * AP buffers frames for 3 beacons instead of 1. */
        esp_wifi_disconnect();
    }
}

net_mode_t net_get_mode(void)
{
    return s_mode;
}

bool net_wait_ip(uint32_t timeout_ms)
{
    return xEventGroupWaitBits(s_events, GOT_IP_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms)) & GOT_IP_BIT;
}

static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint32_t rtt_ms = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &rtt_ms, sizeof(rtt_ms));
    ESP_LOGI(TAG, "keepalive rtt %lu ms", (unsigned long)rtt_ms);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    ESP_LOGW(TAG, "keepalive timeout");
}

void net_keepalive_start(uint32_t interval_ms)
{
    if (s_ping || !s_connected) {
        return;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_netif, &ip) != ESP_OK) {
        return;
    }
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr.type = IPADDR_TYPE_V4;
    cfg.target_addr.u_addr.ip4.addr = ip.gw.addr;
    cfg.count = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms = interval_ms;
    esp_ping_callbacks_t cbs = {
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
    };
    if (esp_ping_new_session(&cfg, &cbs, &s_ping) == ESP_OK) {
        esp_ping_start(s_ping);
    }
}

void net_keepalive_stop(void)
{
    if (s_ping) {
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);
        s_ping = NULL;
    }
}

bool net_connected(void)
{
    return s_connected;
}

int net_rssi(void)
{
    wifi_ap_record_t ap;
    return (s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
}

int64_t net_ip_time_us(void)
{
    return s_ip_time_us;
}

int net_connect_ms(void)
{
    return s_ip_time_us ? (int)((s_ip_time_us - s_start_time_us) / 1000) : -1;
}

bool net_used_fast_connect(void)
{
    return s_used_cache;
}
