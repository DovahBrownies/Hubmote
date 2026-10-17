#pragma once

/*
 * Wall-clock time. The clock is set over the network (SNTP) once Wi-Fi is
 * up. There is no time-zone setting yet: a module that knows the location
 * (Weather) reports its UTC offset, and local time follows it.
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts clock sync; the platform calls it when the network comes up. */
void hub_time_start(void);
/* True once the clock has been set. */
bool hub_time_valid(void);

/* The local time's offset from UTC, in seconds (0 until a module sets it). */
void hub_time_set_utc_offset(int32_t seconds);
int32_t hub_time_utc_offset(void);
/* Converts a UTC time to local broken-down time. */
void hub_time_to_local(time_t t, struct tm *out);
/* Now, local; false while the clock isn't set. */
bool hub_time_local(struct tm *out);

#ifdef __cplusplus
}
#endif
