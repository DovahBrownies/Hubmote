/*
 * The simulator's stand-in for the module platform (components/hub) and the
 * Weather module's data side (modules/weather/weather.c): one module,
 * Weather, showing the forecast from the module's test fixture.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hub_module.h"
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

/* --- Modules ----------------------------------------------------------------- */

static const char *const k_units[] = {"\xC2\xB0" "C, km/h", "\xC2\xB0" "F, mph"};
static const hub_setting_t k_weather_settings[] = {
    {.key = "imperial", .label = "Units", .type = HUB_SETTING_CHOICE, .choices = k_units, .count = 2},
};
static const hub_module_t s_weather = {
    .id = WEATHER_ID,
    .name = "Weather",
    .icon = NULL,
    .create_view = weather_ui_create_view,
    .create_card = weather_ui_create_card,
    .glance = weather_ui_glance,
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

size_t hub_module_count(void)
{
    return s_demo_on ? 2 : 1;
}

const hub_module_t *hub_module_get(size_t index)
{
    return index == 0 ? &s_weather : index == 1 && s_demo_on ? &s_demo : NULL;
}

const hub_module_t *hub_module_find(const char *id)
{
    return !id                                  ? NULL
           : strcmp(id, WEATHER_ID) == 0        ? &s_weather
           : s_demo_on && strcmp(id, "transit") == 0 ? &s_demo
                                                : NULL;
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
void hub_ui_async(void (*fn)(void *arg), void *arg)
{
    fn(arg);
}

/* --- Time: fixed, so screenshots don't change from run to run ----------------- */

static int32_t s_offset;

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
    const hub_setting_t *s = hub_setting_find(module, key);
    return s ? s->def : 0;
}

void hub_setting_str(const hub_module_t *module, const char *key, char *out, size_t len)
{
    snprintf(out, len, "%s", strcmp(key, "stop") == 0 ? "Central Station" : "");
}
