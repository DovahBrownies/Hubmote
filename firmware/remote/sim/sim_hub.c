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

static const hub_module_t s_weather = {
    .id = WEATHER_ID,
    .name = "Weather",
    .icon = NULL,
    .create_view = weather_ui_create_view,
    .create_card = weather_ui_create_card,
    .glance = weather_ui_glance,
};
static void (*s_glance_listener)(void);

size_t hub_module_count(void)
{
    return 1;
}

const hub_module_t *hub_module_get(size_t index)
{
    return index == 0 ? &s_weather : NULL;
}

const hub_module_t *hub_module_find(const char *id)
{
    return id && strcmp(id, WEATHER_ID) == 0 ? &s_weather : NULL;
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
