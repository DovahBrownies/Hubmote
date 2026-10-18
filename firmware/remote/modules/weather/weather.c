/*
 * Weather module: settings, refreshing and caching (the screen is
 * weather_ui.c, the data format weather_data.c).
 *
 * Refreshes: when the network comes up, when the screen or Weather opens and
 * the data is over 10 minutes old, and every 30 minutes while the screen is
 * on. Never while the screen is off. Failed refreshes back off: 1, 2, 5, 15
 * minutes. The last answer is kept in /storage/weather/last.json so the
 * screen fills at once after a restart. Forecasts are always fetched in
 * metric units; the screen converts, so switching units is instant.
 */
#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ui_icons.h"

static const char *TAG = "weather";

#define STALE_US       (30 * 60 * 1000000LL) /* refresh while the screen is on */
#define SHOW_STALE_US  (10 * 60 * 1000000LL) /* refresh when opened or woken */
#define FORECAST_MAX   (24 * 1024)
#define SEARCH_MAX     (12 * 1024)
#define CACHE_FILE     "last.json"

static SemaphoreHandle_t s_lock;
static weather_state_t *s_state; /* PSRAM; guarded by s_lock */
static int64_t s_fetched_us;     /* monotonic time of the last good refresh, 0 = never */
static int64_t s_tried_us;
static int s_fails;
static bool s_net_up;
static bool s_screen_on = true;
static wx_place_t s_found[WX_PLACES_MAX];
static int s_found_count;
static bool s_search_failed;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

void weather_snapshot(weather_state_t *out)
{
    if (!s_state) {
        memset(out, 0, sizeof(*out));
        return;
    }
    lock();
    *out = *s_state;
    unlock();
}

static void ui_update(void)
{
    hub_ui_async(weather_ui_update, NULL);
    hub_glance_changed();
}

/* --- Settings -------------------------------------------------------------- */

static void load_settings(void)
{
    char lat[24], lon[24];
    wx_place_t *p = &s_state->place;
    if (hub_kv_get_str(WEATHER_ID, "name", p->name, sizeof(p->name)) == ESP_OK &&
        hub_kv_get_str(WEATHER_ID, "lat", lat, sizeof(lat)) == ESP_OK &&
        hub_kv_get_str(WEATHER_ID, "lon", lon, sizeof(lon)) == ESP_OK) {
        p->lat = strtod(lat, NULL);
        p->lon = strtod(lon, NULL);
        if (hub_kv_get_str(WEATHER_ID, "region", p->region, sizeof(p->region)) != ESP_OK) {
            p->region[0] = '\0';
        }
        s_state->has_place = true;
    }
    int32_t imperial = 0;
    hub_kv_get_i32(WEATHER_ID, "imperial", &imperial);
    s_state->imperial = imperial != 0;
}

/* --- Refreshing ------------------------------------------------------------ */

static void on_forecast(int status, const char *body, size_t len, void *ctx)
{
    wx_data_t *d = heap_caps_malloc(sizeof(*d), MALLOC_CAP_SPIRAM);
    bool ok = d && status == 200 && wx_parse_forecast(body, d);
    lock();
    s_state->fetching = false;
    s_state->failed = !ok;
    if (ok) {
        s_state->data = *d;
        s_state->fetched = hub_time_valid() ? (int64_t)time(NULL) : d->now.time;
        s_fetched_us = esp_timer_get_time();
        s_fails = 0;
    } else {
        s_fails++;
    }
    unlock();
    if (ok) {
        hub_time_set_utc_offset(d->utc_offset);
        char path[64];
        if (hub_file_path(WEATHER_ID, CACHE_FILE, path, sizeof(path))) {
            hub_file_write(path, body, len);
        }
        ESP_LOGI(TAG, "forecast: %.1f, code %d", d->now.temp, d->now.code);
    } else {
        ESP_LOGW(TAG, "refresh failed (HTTP %d, %u bytes)", status, (unsigned)len);
    }
    free(d);
    ui_update();
}

static int64_t backoff_us(int fails)
{
    static const int minutes[] = {1, 2, 5, 15};
    int i = fails < 1 ? 0 : fails > 4 ? 3 : fails - 1;
    return minutes[i] * 60 * 1000000LL;
}

/* Starts a refresh if the data is older than max_age_us (0: now). */
static void maybe_refresh(int64_t max_age_us)
{
    int64_t now = esp_timer_get_time();
    char url[512];
    lock();
    bool go = s_state->has_place && s_net_up && !s_state->fetching &&
              (s_fetched_us == 0 || max_age_us == 0 || now - s_fetched_us >= max_age_us) &&
              (s_fails == 0 || max_age_us == 0 || now - s_tried_us >= backoff_us(s_fails)) &&
              wx_forecast_url(url, sizeof(url), s_state->place.lat, s_state->place.lon);
    if (go) {
        s_state->fetching = true;
        s_tried_us = now;
    }
    unlock();
    if (!go) {
        return;
    }
    if (hub_http_get(url, FORECAST_MAX, on_forecast, NULL) != ESP_OK) {
        lock();
        s_state->fetching = false;
        s_state->failed = true;
        s_fails++;
        unlock();
    }
    ui_update(); /* shows the spinner, or the failure */
}

/* The screen opened: refresh if stale. */
static void weather_show(void)
{
    maybe_refresh(SHOW_STALE_US);
}

void weather_retry(void)
{
    maybe_refresh(0);
}

/* --- Location search ------------------------------------------------------- */

static void search_results_async(void *arg)
{
    weather_ui_search_results(s_found, s_found_count, s_search_failed);
}

static void on_search(int status, const char *body, size_t len, void *ctx)
{
    s_search_failed = status != 200;
    s_found_count = s_search_failed ? 0 : wx_parse_search(body, s_found, WX_PLACES_MAX);
    hub_ui_async(search_results_async, NULL);
}

void weather_search(const char *text)
{
    char encoded[160], url[320];
    if (!hub_url_encode(text, encoded, sizeof(encoded)) || !wx_search_url(url, sizeof(url), encoded) ||
        hub_http_get(url, SEARCH_MAX, on_search, NULL) != ESP_OK) {
        weather_ui_search_results(NULL, 0, true);
    }
}

void weather_set_place(const wx_place_t *place)
{
    char lat[24], lon[24];
    snprintf(lat, sizeof(lat), "%.5f", place->lat);
    snprintf(lon, sizeof(lon), "%.5f", place->lon);
    hub_kv_set_str(WEATHER_ID, "name", place->name);
    hub_kv_set_str(WEATHER_ID, "region", place->region);
    hub_kv_set_str(WEATHER_ID, "lat", lat);
    hub_kv_set_str(WEATHER_ID, "lon", lon);
    lock();
    s_state->place = *place;
    s_state->has_place = true;
    s_state->data.valid = false;
    s_state->fetched = 0;
    s_state->failed = false;
    s_fetched_us = 0;
    s_fails = 0;
    unlock();
    ESP_LOGI(TAG, "place: %s", place->name);
    ui_update();
    maybe_refresh(0);
}

void weather_set_imperial(bool imperial)
{
    hub_kv_set_i32(WEATHER_ID, "imperial", imperial);
    lock();
    s_state->imperial = imperial;
    unlock();
    ui_update();
}

/* --- Module ----------------------------------------------------------------- */

static void weather_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_state = heap_caps_calloc(1, sizeof(*s_state), MALLOC_CAP_SPIRAM);
    if (!s_lock || !s_state) {
        ESP_LOGE(TAG, "out of memory");
        return;
    }
    load_settings();
    char path[64], *json = NULL;
    if (s_state->has_place && hub_file_path(WEATHER_ID, CACHE_FILE, path, sizeof(path)) &&
        hub_file_read(path, &json, NULL, FORECAST_MAX) == ESP_OK) {
        if (wx_parse_forecast(json, &s_state->data)) {
            s_state->fetched = s_state->data.now.time;
            hub_time_set_utc_offset(s_state->data.utc_offset);
        }
        free(json);
    }
    ESP_LOGI(TAG, "%s%s", s_state->has_place ? s_state->place.name : "no place set",
             s_state->data.valid ? " (cached forecast)" : "");
    ui_update();
}

static void weather_event(hub_event_t event)
{
    if (!s_state) {
        return;
    }
    switch (event) {
    case HUB_EV_NETWORK_UP:
        s_net_up = true;
        if (s_screen_on) { /* a dark screen catches up when it wakes */
            maybe_refresh(STALE_US);
        }
        break;
    case HUB_EV_NETWORK_DOWN:
        s_net_up = false;
        break;
    case HUB_EV_SCREEN_ON:
        s_screen_on = true;
        maybe_refresh(SHOW_STALE_US);
        break;
    case HUB_EV_SCREEN_OFF:
        s_screen_on = false;
        break;
    case HUB_EV_MINUTE:
        if (s_screen_on) {
            maybe_refresh(STALE_US);
        }
        ui_update(); /* "Updated" ages */
        break;
    case HUB_EV_NOW_PLAYING:
        break;
    }
}

/* Units in Settings > Modules, the same value as the screen's °C / °F. */
static const char *const k_units[] = {"\xC2\xB0" "C, km/h", "\xC2\xB0" "F, mph"};
static const hub_setting_t k_settings[] = {
    {.key = "imperial", .label = "Units", .type = HUB_SETTING_CHOICE, .choices = k_units, .count = 2},
};

static void weather_setting_changed(const char *key)
{
    if (!s_state || strcmp(key, "imperial") != 0) {
        return;
    }
    bool imperial = hub_setting_i32(hub_module_find(WEATHER_ID), "imperial") != 0;
    lock();
    s_state->imperial = imperial;
    unlock();
    ui_update();
}

static const hub_module_t s_weather = {
    .id = WEATHER_ID,
    .name = "Weather",
    .icon = ICON_W_CLOUD_SUN,
    .init = weather_init,
    .event = weather_event,
    .create_view = weather_ui_create_view,
    .show = weather_show,
    .create_card = weather_ui_create_card,
    .glance = weather_ui_glance,
    .settings = k_settings,
    .settings_count = sizeof(k_settings) / sizeof(k_settings[0]),
    .setting_changed = weather_setting_changed,
};
HUB_MODULE(s_weather);
