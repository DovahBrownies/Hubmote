#pragma once

/*
 * Weather data from Open-Meteo (https://open-meteo.com): building request
 * URLs, reading the JSON answers, and what the WMO weather codes mean.
 * Plain C and cJSON only, so test/ can run it on a PC.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WX_HOURS 24
#define WX_DAYS  7
#define WX_PLACES_MAX 6

typedef struct {
    int64_t time; /* unix seconds (UTC) */
    float temp;
    int code;     /* WMO weather code */
    int pop;      /* chance of precipitation, %, or -1 */
    bool day;
} wx_hour_t;

typedef struct {
    int64_t time; /* local midnight, as unix seconds */
    int code;
    float tmax, tmin;
    int pop;      /* -1 if unknown */
    int64_t sunrise, sunset;
    float uv;     /* -1 if unknown */
} wx_day_t;

typedef struct {
    bool valid;
    int32_t utc_offset; /* seconds, for the place's local time */
    struct {
        int64_t time;
        float temp, feels, wind;
        int humidity; /* %, or -1 */
        int code;
        bool day;
    } now;
    wx_hour_t hours[WX_HOURS];
    int n_hours;
    wx_day_t days[WX_DAYS];
    int n_days;
} wx_data_t;

typedef struct {
    char name[48];   /* "Sydney" */
    char region[96]; /* "New South Wales, Australia" */
    double lat, lon;
} wx_place_t;

/* What a weather code looks like. The UI maps these to icons and colors. */
typedef enum {
    WX_SUN, WX_MOON, WX_CLOUD_SUN, WX_CLOUD_MOON, WX_CLOUDY, WX_FOG, WX_DRIZZLE, WX_RAIN, WX_HEAVY_RAIN,
    WX_SHOWERS_DAY, WX_SHOWERS_NIGHT, WX_SNOW, WX_HAIL, WX_STORM,
} wx_look_t;

/* Short text ("Partly cloudy") and look for a WMO code at day or night. */
const char *wx_code_text(int code);
wx_look_t wx_code_look(int code, bool day);

/* Always metric (Celsius, km/h); the screen converts, so switching units
 * needs no new request. */
bool wx_forecast_url(char *out, size_t len, double lat, double lon);
/* query must already be URL-encoded. */
bool wx_search_url(char *out, size_t len, const char *encoded_query);

/* Temperatures come out in Celsius and wind in km/h, whatever units the
 * answer used (older caches were fetched in Fahrenheit and mph). */
bool wx_parse_forecast(const char *json, wx_data_t *out);

float wx_c_to_f(float c);
float wx_kmh_to_mph(float kmh);
/* Returns how many places were found (at most max). */
int wx_parse_search(const char *json, wx_place_t *out, int max);
