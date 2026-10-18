#pragma once

/*
 * Wall-clock time. The clock is set over the network (SNTP) once Wi-Fi is
 * up. Local time follows the time zone in Settings > Modules: Automatic
 * (the default) uses the UTC offset a module reports for its place
 * (Weather does), and a zone from the list uses that zone's rules,
 * daylight saving included.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts clock sync; the platform calls it when the network comes up. */
void hub_time_start(void);
/* True once the clock has been set. */
bool hub_time_valid(void);
/* True once local time can be trusted: the clock is set, and a time zone
 * is chosen or (Automatic) a module has reported its place's offset, now
 * or before a restart. Until then local time may be UTC. */
bool hub_time_known(void);

/* Reports the local place's offset from UTC, in seconds. Local time uses
 * it while the time zone is Automatic; it is saved, so local time is right
 * from start-up. */
void hub_time_set_utc_offset(int32_t seconds);
/* The local time's offset from UTC now, in seconds (0 until known). */
int32_t hub_time_utc_offset(void);
/* Converts a UTC time to local broken-down time. */
void hub_time_to_local(time_t t, struct tm *out);
/* Now, local; false while the clock isn't set. */
bool hub_time_local(struct tm *out);

/* The time zones to choose from: IANA names, sorted ("Africa/Abidjan" ...
 * "Pacific/Wallis"), then "UTC". */
size_t hub_time_zone_count(void);
const char *hub_time_zone_name(size_t index);
/* The time zone in use: "" for Automatic. */
const char *hub_time_zone(void);

/* For the platform (main/ and the UI), not for modules. */
/* Reads the saved time zone; call once at start-up. */
void hub_time_init(void);
/* Sets the time zone ("" = Automatic) and saves it. False if the name
 * isn't in the list. Controller task. */
bool hub_time_set_zone(const char *name);

#ifdef __cplusplus
}
#endif
