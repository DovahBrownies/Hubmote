/*
 * Battery level and USB power, from the cell voltage alone.
 *
 * The board doesn't tell the ESP32 when USB power is present: the charger's
 * status pins only drive the CHARGE and DONE LEDs. But the readings give it
 * away. On battery, the CS8501 boosts the cell to 5 V in bursts, and the
 * expander's ADC samples the cell at random points of that ripple: readings
 * swing by 100-300 mV within seconds (seen 2026-10-11). On USB the boost is
 * idle and the charger is linear, so readings stay within a few mV. So:
 *  - USB power = the spread of the last 5 s of readings stays under
 *    QUIET_MV for another 5 s; battery = over NOISY_MV for 1 s. A screen
 *    that is off may draw too little to make the ripple show, so battery ->
 *    USB is only decided with the backlight on and settled.
 *  - The level: on USB the median of those readings; on battery the top of
 *    them (the ripple is dips while the boost draws, so the top is nearest
 *    the cell's own voltage). Smoothed over ~30 s, except in the first 5 s
 *    after a change of power, when the window still holds older readings.
 *  - On USB the charge current lifts the cell's voltage (the lift is
 *    measured at plug-in, and fades as the cell nears 4.2 V), so it is
 *    taken off before the percentage is worked out.
 *  - The shown percentage only falls on battery and only rises on USB,
 *    once a minute has passed since the last change of power.
 * "Charging" means "on USB power": a full cell on USB still shows green.
 *
 * Readings are scaled so the charger's 4.20 V reads 4200: this board's ADC
 * read it as about ADC_FULL_MV (on USB at the end of charge, 2026-10-11).
 */
#include "battery.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "battery";

#define ADC_FULL_MV     4150
#define WIN             25    /* readings, at 5 a second: 5 s */
#define TRIM            2     /* readings left out at each end for the spread */
#define QUIET_MV        40
#define NOISY_MV        80
#define HOLD_TO_USB     WIN   /* readings the new state must hold: quiet for 5 s more */
#define HOLD_TO_BATTERY 5     /* noise is a strong sign: 1 s */
#define SETTLE_US       (6 * 1000000LL)  /* after a backlight change */
#define FREE_US         (60 * 1000000LL) /* after a change of power, the level may move both ways */
#define EMA_DIV         150   /* ~30 s at 5 readings a second */
#define LIFT_DEFAULT_MV 100   /* when started on USB, so the lift couldn't be measured */
#define LIFT_MAX_MV     200
#define FULL_MV         4200
#define REPORT_EVERY_US (5 * 60 * 1000000LL)

static uint16_t s_win[WIN];
static int s_n, s_head;
static int s_bad;
static int s_usb = -1;        /* -1 not known yet, 0 battery, 1 USB */
static int s_hold;            /* readings the other state has held */
static int s_level_x16;       /* smoothed median, mV x 16 */
static int s_lift_mv, s_plug_mv;
static int s_steady_mv;        /* level before a change of power began to show */
static int s_fresh;            /* readings since the last change of power */
static int s_shown = -1;
static int64_t s_changed_us, s_report_us;

static int percent_of(int mv)
{
    static const struct {
        uint16_t mv;
        uint8_t pct;
    } curve[] = {
        {4200, 100}, {4100, 88}, {4000, 77}, {3900, 63}, {3800, 48}, {3750, 38},
        {3700, 25}, {3650, 15}, {3600, 8}, {3500, 3}, {3300, 0},
    };
    if (mv >= curve[0].mv) {
        return 100;
    }
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (mv >= curve[i].mv) {
            int span = curve[i - 1].mv - curve[i].mv;
            return curve[i].pct + (curve[i - 1].pct - curve[i].pct) * (mv - curve[i].mv) / span;
        }
    }
    return 0;
}

/* The cell's resting level: on USB, the reading less what charging adds. */
static int resting_mv(int level)
{
    if (s_usb != 1 || level >= FULL_MV || s_plug_mv >= FULL_MV) {
        return level;
    }
    int lift = s_lift_mv * (FULL_MV - level) / (FULL_MV - s_plug_mv);
    return level - (lift > s_lift_mv ? s_lift_mv : lift);
}

static void set_usb(int usb, int median, int spread, int64_t now_us)
{
    int before = s_steady_mv;
    s_usb = usb;
    s_hold = 0;
    s_changed_us = now_us;
    s_fresh = 0;
    if (usb) {
        int lift = s_shown < 0 ? LIFT_DEFAULT_MV : median - before;
        s_lift_mv = lift < 0 ? 0 : lift > LIFT_MAX_MV ? LIFT_MAX_MV : lift;
        s_plug_mv = median;
        ESP_LOGI(TAG, "on USB (spread %d mV, %d mV, lift %d mV)", spread, median, s_lift_mv);
    } else {
        ESP_LOGI(TAG, "on battery (spread %d mV, %d mV)", spread, median);
    }
}

battery_state_t battery_update(int64_t now_us, uint16_t mv, bool backlight_on, int64_t backlight_changed_us)
{
    /* Outside a Li-ion cell's range: no cell, or a bad read. */
    if (mv < 2500 || mv > 4500) {
        if (++s_bad >= WIN) {
            s_n = s_head = 0;
            s_usb = -1;
            s_shown = -1;
        }
        return (battery_state_t){.percent = s_shown, .charging = s_usb == 1, .no_cell = s_bad >= WIN};
    }
    s_bad = 0;
    s_win[s_head] = (uint16_t)((uint32_t)mv * FULL_MV / ADC_FULL_MV);
    s_head = (s_head + 1) % WIN;
    if (s_n < WIN) {
        s_n++;
        if (s_n < WIN) {
            return (battery_state_t){.percent = s_shown, .charging = s_usb == 1};
        }
    }

    uint16_t sorted[WIN];
    memcpy(sorted, s_win, sizeof(sorted));
    for (int i = 1; i < WIN; i++) {
        uint16_t v = sorted[i];
        int j = i - 1;
        for (; j >= 0 && sorted[j] > v; j--) {
            sorted[j + 1] = sorted[j];
        }
        sorted[j + 1] = v;
    }
    int median = sorted[WIN / 2];
    int spread = sorted[WIN - 1 - TRIM] - sorted[TRIM];

    if (s_usb < 0) {
        s_shown = -1;
        set_usb(spread < (QUIET_MV + NOISY_MV) / 2, median, spread, now_us);
    } else {
        bool settled = backlight_on && now_us - backlight_changed_us >= SETTLE_US;
        bool other = s_usb ? spread > NOISY_MV : (spread < QUIET_MV && settled);
        s_hold = other ? s_hold + 1 : 0;
        if (s_hold >= (s_usb ? HOLD_TO_BATTERY : HOLD_TO_USB)) {
            set_usb(!s_usb, median, spread, now_us);
        }
    }

    int sample = s_usb ? median : sorted[WIN - 1 - TRIM];
    if (s_fresh < WIN) {
        s_fresh++;
        s_level_x16 = sample * 16;
    } else {
        s_level_x16 += (sample * 16 - s_level_x16) / EMA_DIV;
    }
    if (s_hold == 0) {
        s_steady_mv = s_level_x16 / 16;
    }
    int pct = percent_of(resting_mv(s_level_x16 / 16));
    if (s_shown < 0 || now_us - s_changed_us < FREE_US || (s_usb ? pct > s_shown : pct < s_shown)) {
        s_shown = pct;
    }

    if (s_report_us == 0 || now_us - s_report_us >= REPORT_EVERY_US) {
        s_report_us = now_us;
        ESP_LOGI(TAG, "%d mV (median %d, spread %d mV, %s), shown %d%%", s_level_x16 / 16, median, spread,
                 s_usb ? "USB" : "battery", s_shown);
    }
    return (battery_state_t){.percent = s_shown, .charging = s_usb == 1};
}
