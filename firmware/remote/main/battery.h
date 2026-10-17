#pragma once

/*
 * Battery level and charging state, from the cell voltage alone (the board
 * doesn't tell the ESP32 when USB power is present: the charger's status
 * pins only drive the CHARGE and DONE LEDs).
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int percent;   /* 0..100, or -1 until known */
    bool charging; /* on USB power (charging, or full and held there) */
    bool no_cell;  /* no cell connected (readings out of range): USB power too */
} battery_state_t;

/* Feed one reading every 200 ms. backlight_changed_us: when the backlight
 * level last changed (esp_timer time, 0 if never). */
battery_state_t battery_update(int64_t now_us, uint16_t mv, bool backlight_on, int64_t backlight_changed_us);
