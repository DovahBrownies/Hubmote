/*
 * The simulator's stand-in for the module platform (components/hub) and the
 * Weather module's data side (modules/weather/weather.c): Weather, showing
 * the forecast from the module's test fixture, and the modules that
 * register themselves (Screen time, the Sleep timer, Guest Wi-Fi, Internet
 * speed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hub_actions.h"
#include "hub_module.h"
#include "hub_store.h"
#include "hub_time.h"
#include "hub_tz.h"
#include "ui_icons.h"
#include "weather.h"

size_t sim_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);
    if (size) {
        size_t k = n < size - 1 ? n : size - 1;
        memcpy(dst, src, k);
        dst[k] = '\0';
    }
    return n;
}

size_t sim_strlcat(char *dst, const char *src, size_t size)
{
    size_t used = strnlen(dst, size);
    return used + (used < size ? sim_strlcpy(dst + used, src, size - used) : strlen(src));
}

/* --- Modules ----------------------------------------------------------------- */

static const char *const k_units[] = {"\xC2\xB0" "C, km/h", "\xC2\xB0" "F, mph"};
static const hub_setting_t k_weather_settings[] = {
    {.key = "imperial", .label = "Units", .type = HUB_SETTING_CHOICE, .choices = k_units, .count = 2},
};
static const hub_module_t s_weather = {
    .id = WEATHER_ID,
    .name = "Weather",
    .icon = ICON_W_CLOUD_SUN, /* as modules/weather/weather.c */
    .create_view = weather_ui_create_view,
    .create_card = weather_ui_create_card,
    .glance = weather_ui_glance,
    .card_sizes = HUB_CARD_SMALL | HUB_CARD_MEDIUM | HUB_CARD_LARGE,
    .card_size = HUB_CARD_LARGE,
    .settings = k_weather_settings,
    .settings_count = 1,
};

/* A module with a setting of each kind, so Settings draws them all. */
static const char *const k_lines[] = {"T1 Northern", "T2 Inner West", "T3 Bankstown", "T4 Eastern Suburbs"};
static const hub_setting_t k_demo_settings[] = {
    {.key = "line", .label = "Line", .type = HUB_SETTING_CHOICE, .choices = k_lines, .count = 4, .def = 1},
    {.key = "walk", .label = "Walk to the stop", .type = HUB_SETTING_NUMBER, .def = 6, .min = 0, .max = 30,
     .step = 1, .unit = "min"},
    {.key = "stop", .label = "Stop", .type = HUB_SETTING_TEXT, .max_len = 40},
    {.key = "late", .label = "Show late services", .type = HUB_SETTING_SWITCH, .def = 1},
};
static const hub_module_t s_demo = {
    .id = "transit",
    .name = "Transit",
    .icon = ICON_MAP_PIN,
    .settings = k_demo_settings,
    .settings_count = sizeof(k_demo_settings) / sizeof(k_demo_settings[0]),
};
static bool s_demo_on; /* only while drawing Settings, so the other screens stay as they were */
static void (*s_glance_listener)(void);

void sim_show_demo_module(bool on)
{
    s_demo_on = on;
}

/* Modules that register themselves (HUB_MODULE), as on the remote. */
static const hub_module_t *s_registered[HUB_MODULES_MAX];
static size_t s_registered_count;

void hub_module_register(const hub_module_t *module)
{
    if (s_registered_count < sizeof(s_registered) / sizeof(s_registered[0])) {
        s_registered[s_registered_count++] = module;
    }
}

static int by_name(const void *a, const void *b)
{
    return strcmp((*(const hub_module_t *const *)a)->name, (*(const hub_module_t *const *)b)->name);
}

/* Every module, sorted by name as hub_modules_init() does. */
static size_t modules(const hub_module_t **out)
{
    size_t n = 0;
    out[n++] = &s_weather;
    for (size_t i = 0; i < s_registered_count; i++) {
        out[n++] = s_registered[i];
    }
    if (s_demo_on) {
        out[n++] = &s_demo;
    }
    qsort(out, n, sizeof(out[0]), by_name);
    return n;
}

size_t hub_module_count(void)
{
    const hub_module_t *list[HUB_MODULES_MAX + 2]; /* the registered, Weather, the demo */
    return modules(list);
}

const hub_module_t *hub_module_get(size_t index)
{
    const hub_module_t *list[HUB_MODULES_MAX + 2]; /* the registered, Weather, the demo */
    return index < modules(list) ? list[index] : NULL;
}

const hub_module_t *hub_module_find(const char *id)
{
    const hub_module_t *list[HUB_MODULES_MAX + 2]; /* the registered, Weather, the demo */
    size_t n = modules(list);
    for (size_t i = 0; id && i < n; i++) {
        if (strcmp(list[i]->id, id) == 0) {
            return list[i];
        }
    }
    return NULL;
}

/* Actions on the TV do nothing here. */
esp_err_t hub_tv_off(bool ir)
{
    (void)ir;
    return ESP_OK;
}

esp_err_t hub_tv_key(hub_key_t key)
{
    (void)key;
    return ESP_OK;
}

void hub_hold_awake(void)
{
}

void hub_release_awake(void)
{
}

bool hub_module_enabled(const hub_module_t *module)
{
    return module != NULL;
}

void hub_module_set_enabled(const char *id, bool on)
{
    (void)id;
    (void)on;
}

void hub_set_modules_listener(void (*listener)(void))
{
    (void)listener;
}

void hub_set_glance_listener(void (*listener)(void))
{
    s_glance_listener = listener;
}

void hub_glance_changed(void)
{
    if (s_glance_listener) {
        s_glance_listener();
    }
}

/* Everything runs on one thread here: run it now. */
bool hub_ui_async(void (*fn)(void *arg), void *arg)
{
    fn(arg);
    return true;
}

/* --- Time: fixed, so screenshots don't change from run to run ----------------- */

static int32_t s_offset;

static bool s_12h;

bool hub_time_12h(void)
{
    return s_12h;
}

void hub_time_set_12h(bool on)
{
    s_12h = on;
}

bool hub_time_known(void)
{
    return true;
}

bool hub_time_valid(void)
{
    return false; /* nothing counts as stale */
}

void hub_time_set_utc_offset(int32_t seconds)
{
    s_offset = seconds;
}

int32_t hub_time_utc_offset(void)
{
    return s_offset;
}

void hub_time_to_local(time_t t, struct tm *out)
{
    time_t local = t + s_offset;
    *out = *gmtime(&local);
}

bool hub_time_local(struct tm *out)
{
    (void)out;
    return false;
}

/* The real zone list (hub_tz.c); the zone itself stays Automatic. */
size_t hub_time_zone_count(void)
{
    return hub_tz_count;
}

const char *hub_time_zone_name(size_t index)
{
    return index < hub_tz_count ? hub_tz[index].name : NULL;
}

const char *hub_time_zone(void)
{
    return "";
}

/* --- Weather's data -------------------------------------------------------------- */

static weather_state_t s_state;

/* Loads the module's test forecast (SIM_FIXTURE, Sydney). */
void sim_weather_load(void)
{
    FILE *f = fopen(SIM_FIXTURE, "rb");
    if (!f) {
        fprintf(stderr, "sim: can't open %s\n", SIM_FIXTURE);
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = calloc(1, (size_t)n + 1);
    if (json && fread(json, 1, (size_t)n, f) == (size_t)n && wx_parse_forecast(json, &s_state.data)) {
        s_state.has_place = true;
        strcpy(s_state.place.name, "Sydney");
        strcpy(s_state.place.region, "New South Wales, Australia");
        s_state.fetched = s_state.data.now.time;
        s_offset = s_state.data.utc_offset;
    }
    free(json);
    fclose(f);
}

void weather_snapshot(weather_state_t *out)
{
    *out = s_state;
}

void weather_retry(void)
{
}

void weather_search(const char *text)
{
    (void)text;
}

void weather_set_place(const wx_place_t *place)
{
    (void)place;
}

void weather_set_imperial(bool imperial)
{
    s_state.imperial = imperial;
}

/* --- Module settings: their defaults --------------------------------------------- */

const hub_setting_t *hub_setting_find(const hub_module_t *module, const char *key)
{
    for (size_t i = 0; module && i < module->settings_count; i++) {
        if (strcmp(module->settings[i].key, key) == 0) {
            return &module->settings[i];
        }
    }
    return NULL;
}

int32_t hub_setting_i32(const hub_module_t *module, const char *key)
{
    if (module && strcmp(module->id, "screen_time") == 0 && strcmp(key, "limit") == 0) {
        return 120; /* a limit, so its screen shows one */
    }
    const hub_setting_t *s = hub_setting_find(module, key);
    return s ? s->def : 0;
}

/* --- The TV and module storage: Screen time's day, half used ------------------- */

esp_err_t hub_notify(const hub_module_t *module, const char *icon, const char *text)
{
    (void)module;
    (void)icon;
    (void)text;
    return ESP_OK;
}

esp_err_t hub_tv_notify(const char *text)
{
    (void)text;
    return ESP_OK;
}

void hub_tv_state(hub_tv_state_t *out)
{
    memset(out, 0, sizeof(*out));
    out->on = true;
    strlcpy(out->app, "com.netflix.ninja", sizeof(out->app));
}

bool hub_now_playing(hub_now_playing_t *out)
{
    (void)out;
    return false;
}

size_t hub_tv_apps(hub_tv_app_t *out, size_t max)
{
    static const hub_tv_app_t apps[] = {
        {"com.netflix.ninja", "Netflix"},
        {"com.google.android.youtube.tv", "YouTube"},
        {"com.plexapp.android", "Plex"},
        {"com.spotify.tv.android", "Spotify"},
    };
    size_t n = sizeof(apps) / sizeof(apps[0]) < max ? sizeof(apps) / sizeof(apps[0]) : max;
    memcpy(out, apps, n * sizeof(apps[0]));
    return n;
}

esp_err_t hub_kv_get_i32(const char *module, const char *key, int32_t *out)
{
    if (strcmp(module, "screen_time") == 0 && strcmp(key, "used") == 0) {
        *out = 95;
        return ESP_OK;
    }
    return ESP_FAIL;
}

/* The TV's last speed test, finished */
void hub_speedtest(hub_speedtest_t *out)
{
    *out = (hub_speedtest_t){.state = HUB_SPEED_DONE, .progress = 100, .ping_ms = 11, .down_mbps = 312.4f,
                             .up_mbps = 41.2f};
}

esp_err_t hub_tv_speedtest(void)
{
    return ESP_OK;
}

esp_err_t hub_kv_get_str(const char *module, const char *key, char *out, size_t len)
{
    if (strcmp(module, "speed_test") == 0 && strcmp(key, "hist") == 0) {
        /* No clock in the simulator's results (0): they show "-" for the time */
        strlcpy(out, "0,8.6,2.1,48.0;0,312.4,41.2,11.0;0,298.0,39.8,12.0", len); /* the latest under 10: a decimal point in the big font */
        return ESP_OK;
    }
    if (strcmp(module, "screen_time") == 0 && strcmp(key, "apps") == 0) {
        strlcpy(out,
                "com.netflix.ninja=48;com.google.android.youtube.tv=27;com.google.android.apps.tv.launcherx=12;"
                "com.plexapp.android=8;",
                len);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t hub_kv_set_i32(const char *module, const char *key, int32_t value)
{
    (void)module;
    (void)key;
    (void)value;
    return ESP_OK;
}

esp_err_t hub_kv_set_str(const char *module, const char *key, const char *value)
{
    (void)module;
    (void)key;
    (void)value;
    return ESP_OK;
}

void hub_setting_str(const hub_module_t *module, const char *key, char *out, size_t len)
{
    (void)module;
    snprintf(out, len, "%s",
             strcmp(key, "stop") == 0   ? "Central Station"
             : strcmp(key, "ssid") == 0 ? "Hubmote Guest"
             : strcmp(key, "pass") == 0 ? "welcome-home-42"
                                        : "");
}
