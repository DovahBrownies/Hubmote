#pragma once

/*
 * When this firmware was built, to the minute ("Oct 13 2026", "23:10").
 * ESP-IDF's own esp_app_desc date and time only change when ESP-IDF
 * recompiles that file, which most builds don't, so use these instead.
 */
const char *build_date(void);
const char *build_time(void);
