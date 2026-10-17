/* Open-Meteo URLs, JSON parsing and WMO weather codes (host-testable). */
#include "weather_data.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"

/* --- WMO weather codes (Open-Meteo's list) --------------------------------- */

const char *wx_code_text(int code)
{
    switch (code) {
    case 0: return "Clear";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: return "Fog";
    case 48: return "Freezing fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Heavy drizzle";
    case 56:
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66:
    case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: return "Snow showers";
    case 86: return "Heavy snow showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Thunderstorm, hail";
    default: return "";
    }
}

wx_look_t wx_code_look(int code, bool day)
{
    switch (code) {
    case 0: return day ? WX_SUN : WX_MOON;
    case 1:
    case 2: return day ? WX_CLOUD_SUN : WX_CLOUD_MOON;
    case 45:
    case 48: return WX_FOG;
    case 51: case 53: case 55: case 56: case 57: return WX_DRIZZLE;
    case 61: case 63: case 66: return WX_RAIN;
    case 65: case 67: case 82: return WX_HEAVY_RAIN;
    case 71: case 73: case 75: case 77: case 85: case 86: return WX_SNOW;
    case 80:
    case 81: return day ? WX_SHOWERS_DAY : WX_SHOWERS_NIGHT;
    case 95: return WX_STORM;
    case 96:
    case 99: return WX_HAIL;
    default: return WX_CLOUDY; /* 3, and anything unknown */
    }
}

/* --- URLs -------------------------------------------------------------------- */

bool wx_forecast_url(char *out, size_t len, double lat, double lon)
{
    int n = snprintf(out, len,
                     "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                     "&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,"
                     "wind_speed_10m"
                     "&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
                     "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
                     "sunrise,sunset,uv_index_max"
                     "&timezone=auto&forecast_days=%d&forecast_hours=%d&timeformat=unixtime",
                     lat, lon, WX_DAYS, WX_HOURS);
    return n > 0 && (size_t)n < len;
}

bool wx_search_url(char *out, size_t len, const char *encoded_query)
{
    int n = snprintf(out, len,
                     "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=en&format=json",
                     encoded_query, WX_PLACES_MAX);
    return n > 0 && (size_t)n < len;
}

/* --- JSON -------------------------------------------------------------------- */

static double num(const cJSON *obj, const char *key, double fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

/* Copies a JSON array of numbers (nulls become fallback); returns its length. */
static int numbers(const cJSON *obj, const char *key, double *out, int max, double fallback)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    int n = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, arr)
    {
        if (n >= max) {
            break;
        }
        out[n++] = cJSON_IsNumber(v) ? v->valuedouble : fallback;
    }
    return n;
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

float wx_c_to_f(float c)
{
    return c * 9.0f / 5.0f + 32.0f;
}

float wx_kmh_to_mph(float kmh)
{
    return kmh / 1.609344f;
}

static bool unit_is(const cJSON *root, const char *key, const char *unit)
{
    const cJSON *units = cJSON_GetObjectItemCaseSensitive(root, "current_units");
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(units, key);
    return cJSON_IsString(v) && v->valuestring && strcmp(v->valuestring, unit) == 0;
}

static float f_to_c(float f)
{
    return (f - 32.0f) * 5.0f / 9.0f;
}

/* Back to Celsius and km/h if the answer was in Fahrenheit or mph. */
static void to_metric(const cJSON *root, wx_data_t *d)
{
    if (unit_is(root, "temperature_2m", "\xC2\xB0" "F")) {
        d->now.temp = f_to_c(d->now.temp);
        d->now.feels = f_to_c(d->now.feels);
        for (int i = 0; i < d->n_hours; i++) {
            d->hours[i].temp = f_to_c(d->hours[i].temp);
        }
        for (int i = 0; i < d->n_days; i++) {
            d->days[i].tmax = f_to_c(d->days[i].tmax);
            d->days[i].tmin = f_to_c(d->days[i].tmin);
        }
    }
    if (unit_is(root, "wind_speed_10m", "mph")) {
        d->now.wind *= 1.609344f;
    }
}

bool wx_parse_forecast(const char *json, wx_data_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return false;
    }
    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(cur) || !cJSON_IsObject(hourly) || !cJSON_IsObject(daily)) {
        cJSON_Delete(root);
        return false;
    }

    out->utc_offset = (int32_t)num(root, "utc_offset_seconds", 0);
    out->now.time = (int64_t)num(cur, "time", 0);
    out->now.temp = (float)num(cur, "temperature_2m", 0);
    out->now.feels = (float)num(cur, "apparent_temperature", out->now.temp);
    out->now.wind = (float)num(cur, "wind_speed_10m", 0);
    out->now.humidity = (int)num(cur, "relative_humidity_2m", -1);
    out->now.code = (int)num(cur, "weather_code", 3);
    out->now.day = num(cur, "is_day", 1) != 0;

    double t[WX_HOURS], temp[WX_HOURS], code[WX_HOURS], pop[WX_HOURS], day[WX_HOURS];
    int n = numbers(hourly, "time", t, WX_HOURS, 0);
    n = min_int(n, numbers(hourly, "temperature_2m", temp, WX_HOURS, 0));
    n = min_int(n, numbers(hourly, "weather_code", code, WX_HOURS, 3));
    int np = numbers(hourly, "precipitation_probability", pop, WX_HOURS, -1);
    int nd = numbers(hourly, "is_day", day, WX_HOURS, 1);
    for (int i = 0; i < n; i++) {
        out->hours[i] = (wx_hour_t){
            .time = (int64_t)t[i],
            .temp = (float)temp[i],
            .code = (int)code[i],
            .pop = i < np ? (int)pop[i] : -1,
            .day = i < nd ? day[i] != 0 : true,
        };
    }
    out->n_hours = n;

    double dt[WX_DAYS], dcode[WX_DAYS], tmax[WX_DAYS], tmin[WX_DAYS], dpop[WX_DAYS], rise[WX_DAYS], set[WX_DAYS],
        uv[WX_DAYS];
    int m = numbers(daily, "time", dt, WX_DAYS, 0);
    m = min_int(m, numbers(daily, "weather_code", dcode, WX_DAYS, 3));
    m = min_int(m, numbers(daily, "temperature_2m_max", tmax, WX_DAYS, 0));
    m = min_int(m, numbers(daily, "temperature_2m_min", tmin, WX_DAYS, 0));
    int mp = numbers(daily, "precipitation_probability_max", dpop, WX_DAYS, -1);
    int mr = numbers(daily, "sunrise", rise, WX_DAYS, 0);
    int ms = numbers(daily, "sunset", set, WX_DAYS, 0);
    int mu = numbers(daily, "uv_index_max", uv, WX_DAYS, -1);
    for (int i = 0; i < m; i++) {
        out->days[i] = (wx_day_t){
            .time = (int64_t)dt[i],
            .code = (int)dcode[i],
            .tmax = (float)tmax[i],
            .tmin = (float)tmin[i],
            .pop = i < mp ? (int)dpop[i] : -1,
            .sunrise = i < mr ? (int64_t)rise[i] : 0,
            .sunset = i < ms ? (int64_t)set[i] : 0,
            .uv = i < mu ? (float)uv[i] : -1,
        };
    }
    out->n_days = m;
    to_metric(root, out);
    out->valid = out->now.time > 0 && n > 0 && m > 0;
    cJSON_Delete(root);
    return out->valid;
}

static void copy_str(char *dst, size_t len, const cJSON *v)
{
    if (cJSON_IsString(v) && v->valuestring) {
        snprintf(dst, len, "%s", v->valuestring);
    } else {
        dst[0] = '\0';
    }
}

int wx_parse_search(const char *json, wx_place_t *out, int max)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return 0;
    }
    int n = 0;
    const cJSON *r;
    cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(root, "results"))
    {
        if (n >= max) {
            break;
        }
        const cJSON *lat = cJSON_GetObjectItemCaseSensitive(r, "latitude");
        const cJSON *lon = cJSON_GetObjectItemCaseSensitive(r, "longitude");
        if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
            continue;
        }
        wx_place_t *p = &out[n++];
        copy_str(p->name, sizeof(p->name), cJSON_GetObjectItemCaseSensitive(r, "name"));
        char admin[46], country[46]; /* + ", " fits region */
        copy_str(admin, sizeof(admin), cJSON_GetObjectItemCaseSensitive(r, "admin1"));
        copy_str(country, sizeof(country), cJSON_GetObjectItemCaseSensitive(r, "country"));
        snprintf(p->region, sizeof(p->region), "%s%s%s", admin, admin[0] && country[0] ? ", " : "", country);
        p->lat = lat->valuedouble;
        p->lon = lon->valuedouble;
    }
    cJSON_Delete(root);
    return n;
}
