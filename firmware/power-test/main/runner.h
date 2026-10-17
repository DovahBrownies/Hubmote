#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "net.h"

/* Everything the screen-on states vary. One field per measurement axis. */
typedef struct {
    uint8_t backlight_pct; /* 0 = backlight driver off */
    uint16_t cpu_mhz;      /* 80 / 160 / 240, fixed (no DFS) */
    uint8_t pclk_mhz;      /* RGB pixel clock */
    bool animate;          /* continuous full-screen redraw */
    net_mode_t wifi;
} pt_settings_t;

/* Screen-off states. Each ends in a reboot (or deep-sleep wake) back to the
 * menu, which reports what just ran. RESET aborts any of them. */
typedef enum {
    PT_TEST_NONE = 0,
    PT_TEST_BACKLIGHT_OFF,    /* backlight off, panel still scanning */
    PT_TEST_PANEL_OFF,        /* + panel bias off, RGB peripheral deleted, CPU idle */
    PT_TEST_TOUCH_SLEEP,      /* + GT911 in sleep */
    PT_TEST_LIGHT_SLEEP,      /* + automatic light sleep, radio off */
    PT_TEST_WARM_STANDBY,     /* light sleep + Wi-Fi associated + 5 s keepalive */
    PT_TEST_DEEP_SLEEP,       /* deep sleep, LCD pins as the driver left them */
    PT_TEST_DEEP_SLEEP_PARKED,/* deep sleep, LCD pins driven low and held */
    PT_TEST_WAKE_LATENCY,     /* 3 s deep sleep, then time the cold wake */
    PT_TEST_COUNT
} pt_test_t;

/* Restores settings and sequence progress kept across the last reboot. */
void runner_init(void);
/* Starts the runner task; resumes a sequence interrupted by a reboot. */
void runner_start(void);

const pt_settings_t *runner_settings(void);
/* Applies settings. Must be called with the LVGL port lock held. */
void runner_apply(const pt_settings_t *s);

void runner_start_test(pt_test_t test);
void runner_start_sequence(void);
void runner_abort(void);
bool runner_busy(void);

uint16_t runner_duration_s(void);
void runner_set_duration_s(uint16_t seconds);

const char *runner_test_name(pt_test_t test);
/* Multi-line text about this boot: reset reason, what just finished, timings. */
const char *runner_boot_note(void);
/* Call once the first frame is visible, to time the boot. */
void runner_mark_first_frame(void);
