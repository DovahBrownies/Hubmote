/*
 * Host test for weather_data.c: parses real Open-Meteo answers saved in
 * this folder and checks the WMO code table and URLs.
 *
 *   python run_test.py        (or see the gcc line in run_test.py)
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "weather_data.h"

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "can't open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (n < 0 || !buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "can't read %s\n", path);
        exit(1);
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void test_forecast(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/forecast_sydney.json", dir);
    char *json = slurp(path);
    wx_data_t d;
    assert(wx_parse_forecast(json, &d));
    assert(d.valid);
    assert(d.utc_offset == 36000 || d.utc_offset == 39600); /* Sydney: +10 or +11 (DST) */
    assert(d.now.time > 1700000000);
    assert(d.now.temp > -30 && d.now.temp < 55);
    assert(d.now.humidity >= 0 && d.now.humidity <= 100);
    assert(d.n_hours == WX_HOURS);
    assert(d.n_days == WX_DAYS);
    for (int i = 1; i < d.n_hours; i++) {
        assert(d.hours[i].time - d.hours[i - 1].time == 3600);
    }
    for (int i = 0; i < d.n_days; i++) {
        assert(d.days[i].tmax >= d.days[i].tmin);
        assert(d.days[i].sunset > d.days[i].sunrise);
    }
    printf("forecast: now %.1f (code %d, %s), %d hours, %d days, offset %d\n", d.now.temp, d.now.code,
           wx_code_text(d.now.code), d.n_hours, d.n_days, (int)d.utc_offset);
    free(json);

    /* Broken or partial answers are refused, never half-read. */
    assert(!wx_parse_forecast("", &d));
    assert(!wx_parse_forecast("{\"error\":true,\"reason\":\"x\"}", &d));
    assert(!wx_parse_forecast("{\"current\":{},\"hourly\":{},\"daily\":{}}", &d));
}

/* A cache from before metric-only fetching: Fahrenheit and mph become
 * Celsius and km/h. */
static void test_imperial_answer(void)
{
    static const char json[] =
        "{\"utc_offset_seconds\":0,"
        "\"current_units\":{\"temperature_2m\":\"\xC2\xB0" "F\",\"wind_speed_10m\":\"mph\"},"
        "\"current\":{\"time\":1760000000,\"temperature_2m\":68.0,\"apparent_temperature\":50.0,"
        "\"wind_speed_10m\":10.0,\"weather_code\":0,\"is_day\":1},"
        "\"hourly\":{\"time\":[1760000000],\"temperature_2m\":[32.0],\"weather_code\":[0]},"
        "\"daily\":{\"time\":[1759968000],\"weather_code\":[0],\"temperature_2m_max\":[86.0],"
        "\"temperature_2m_min\":[14.0]}}";
    wx_data_t d;
    assert(wx_parse_forecast(json, &d));
#define NEAR(a, b) ((a) - (b) < 0.01f && (b) - (a) < 0.01f)
    assert(NEAR(d.now.temp, 20.0f));
    assert(NEAR(d.now.feels, 10.0f));
    assert(NEAR(d.hours[0].temp, 0.0f));
    assert(NEAR(d.days[0].tmax, 30.0f));
    assert(NEAR(d.days[0].tmin, -10.0f));
    assert(NEAR(d.now.wind, 16.09344f));
    assert(NEAR(wx_c_to_f(20.0f), 68.0f));
    assert(NEAR(wx_kmh_to_mph(16.09344f), 10.0f));
#undef NEAR
}

static void test_search(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/search_sydney.json", dir);
    char *json = slurp(path);
    wx_place_t p[WX_PLACES_MAX];
    int n = wx_parse_search(json, p, WX_PLACES_MAX);
    assert(n > 0);
    assert(strcmp(p[0].name, "Sydney") == 0);
    assert(p[0].lat < -33 && p[0].lat > -34.5);
    printf("search: %d places, first %s (%s)\n", n, p[0].name, p[0].region);
    free(json);
    assert(wx_parse_search("{}", p, WX_PLACES_MAX) == 0);
    assert(wx_parse_search("not json", p, WX_PLACES_MAX) == 0);
}

static void test_codes(void)
{
    assert(wx_code_look(0, true) == WX_SUN);
    assert(wx_code_look(0, false) == WX_MOON);
    assert(wx_code_look(2, true) == WX_CLOUD_SUN);
    assert(wx_code_look(3, true) == WX_CLOUDY);
    assert(wx_code_look(45, true) == WX_FOG);
    assert(wx_code_look(63, true) == WX_RAIN);
    assert(wx_code_look(65, true) == WX_HEAVY_RAIN);
    assert(wx_code_look(80, false) == WX_SHOWERS_NIGHT);
    assert(wx_code_look(95, true) == WX_STORM);
    assert(wx_code_look(99, true) == WX_HAIL);
    assert(wx_code_look(1234, true) == WX_CLOUDY);
    static const int known[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77,
                                80, 81, 82, 85, 86, 95, 96, 99};
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        assert(wx_code_text(known[i])[0] != '\0');
    }
}

static void test_urls(void)
{
    char url[512];
    assert(wx_forecast_url(url, sizeof(url), -33.8688, 151.2093));
    assert(strstr(url, "latitude=-33.8688") && strstr(url, "longitude=151.2093"));
    assert(!strstr(url, "fahrenheit") && !strstr(url, "mph")); /* always metric */
    assert(!wx_forecast_url(url, 64, 0, 0)); /* too small: refused, not cut */
    assert(wx_search_url(url, sizeof(url), "New%20York"));
    assert(strstr(url, "name=New%20York"));
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    test_codes();
    test_urls();
    test_forecast(dir);
    test_imperial_answer();
    test_search(dir);
    printf("all weather_data tests passed\n");
    return 0;
}
