/* Clock sync (SNTP) and local time: a time zone's rules, or a reported UTC
 * offset while the zone is Automatic. */
#include "hub_time.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hub_tz.h"
#include "kv.h"

static const char *TAG = "hub_time";

#define VALID_AFTER 1700000000 /* 2023-11: anything earlier is the unset clock */
#define NVS_NS      "hub"
#define NVS_ZONE    "tz"
#define NVS_OFFSET  "utc_off" /* the last offset reported, so local time is right from start-up */
#define NVS_CLOCK   "clock12" /* 1: 12-hour */

static bool s_started;
static int32_t s_offset;
static bool s_offset_known; /* reported, or saved from an earlier report */
static const hub_tz_t *s_zone; /* NULL: Automatic */
static bool s_12h;
/* TZ and localtime_r() are the C library's shared state: the zone can
 * change on the controller task while a module reads the time on another. */
static SemaphoreHandle_t s_lock;

static void lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static int by_name(const void *key, const void *item)
{
    return strcmp(key, ((const hub_tz_t *)item)->name);
}

static const hub_tz_t *find(const char *name)
{
    return bsearch(name, hub_tz, hub_tz_count, sizeof(hub_tz[0]), by_name);
}

static void apply(const hub_tz_t *zone)
{
    lock();
    s_zone = zone;
    if (zone) {
        setenv("TZ", zone->posix, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    unlock();
}

void hub_time_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    s_offset_known = kv_get_i32(NVS_NS, NVS_OFFSET, &s_offset) == ESP_OK;
    int32_t clock12 = 0;
    kv_get_i32(NVS_NS, NVS_CLOCK, &clock12);
    s_12h = clock12 != 0;
    char name[48];
    kv_get_str(NVS_NS, NVS_ZONE, name, sizeof(name));
    const hub_tz_t *zone = name[0] ? find(name) : NULL;
    apply(zone);
    ESP_LOGI(TAG, "time zone: %s", zone ? zone->name : "automatic");
}

bool hub_time_set_zone(const char *name)
{
    const hub_tz_t *zone = NULL;
    if (name[0]) {
        zone = find(name);
        if (!zone) {
            return false;
        }
    }
    apply(zone);
    if (zone) {
        kv_set_str(NVS_NS, NVS_ZONE, zone->name);
    } else {
        kv_erase(NVS_NS, NVS_ZONE);
    }
    ESP_LOGI(TAG, "time zone: %s", zone ? zone->name : "automatic");
    return true;
}

bool hub_time_12h(void)
{
    return s_12h;
}

void hub_time_set_12h(bool on)
{
    s_12h = on;
    kv_set_i32(NVS_NS, NVS_CLOCK, on);
    ESP_LOGI(TAG, "clock: %s", on ? "12-hour" : "24-hour");
}

const char *hub_time_zone(void)
{
    return s_zone ? s_zone->name : "";
}

size_t hub_time_zone_count(void)
{
    return hub_tz_count;
}

const char *hub_time_zone_name(size_t index)
{
    return index < hub_tz_count ? hub_tz[index].name : NULL;
}

void hub_time_start(void)
{
    if (s_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    s_started = err == ESP_OK;
    ESP_LOGI(TAG, "clock sync: %s", esp_err_to_name(err));
}

bool hub_time_known(void)
{
    return hub_time_valid() && (s_zone || s_offset_known);
}

bool hub_time_valid(void)
{
    return time(NULL) > VALID_AFTER;
}

void hub_time_set_utc_offset(int32_t seconds)
{
    if (seconds != s_offset || !s_offset_known) {
        s_offset = seconds;
        s_offset_known = true;
        kv_set_i32(NVS_NS, NVS_OFFSET, seconds);
    }
}

int32_t hub_time_utc_offset(void)
{
    if (!s_zone) {
        return s_offset;
    }
    time_t now = time(NULL);
    struct tm local, utc;
    hub_time_to_local(now, &local);
    gmtime_r(&now, &utc);
    int days = local.tm_year != utc.tm_year ? (local.tm_year > utc.tm_year ? 1 : -1) : local.tm_yday - utc.tm_yday;
    return days * 86400 + (local.tm_hour - utc.tm_hour) * 3600 + (local.tm_min - utc.tm_min) * 60 +
           (local.tm_sec - utc.tm_sec);
}

void hub_time_to_local(time_t t, struct tm *out)
{
    if (s_zone) {
        lock();
        localtime_r(&t, out);
        unlock();
    } else {
        time_t local = t + s_offset;
        gmtime_r(&local, out);
    }
}

bool hub_time_local(struct tm *out)
{
    if (!hub_time_valid()) {
        return false;
    }
    hub_time_to_local(time(NULL), out);
    return true;
}
