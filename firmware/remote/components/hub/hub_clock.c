/* Times as the clock setting shows them (hub_time.h): 12-hour or 24-hour.
 * No ESP-IDF here, so the UI simulator builds it too. */
#include <stdio.h>
#include "hub_time.h"

static int hour12(int hour)
{
    return hour % 12 ? hour % 12 : 12;
}

void hub_time_fmt_hm(char *out, size_t len, int hour, int min)
{
    if (hub_time_12h()) {
        snprintf(out, len, "%d:%02d %s", hour12(hour), min, hour < 12 ? "AM" : "PM");
    } else {
        snprintf(out, len, "%02d:%02d", hour, min);
    }
}

void hub_time_fmt_hour(char *out, size_t len, int hour)
{
    if (hub_time_12h()) {
        snprintf(out, len, "%d %s", hour12(hour), hour < 12 ? "AM" : "PM");
    } else {
        snprintf(out, len, "%02d:00", hour);
    }
}
