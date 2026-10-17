/*
 * One-screen control panel: segmented controls for each screen-on
 * measurement axis, buttons for the screen-off tests, and a live readout.
 */
#include "ui.h"

#include <stdio.h>
#include "board.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "net.h"
#include "runner.h"

#define COLOR_BG      lv_color_hex(0x0F1216)
#define COLOR_CARD    lv_color_hex(0x1A1F26)
#define COLOR_MUTED   lv_color_hex(0x8A94A3)
#define COLOR_TEXT    lv_color_hex(0xE6EAF0)

#define RIGHT_X   360
#define RIGHT_W   652
#define ROW_PITCH 66

static const uint8_t BL_VALUES[] = {0, 1, 10, 25, 50, 75, 100};
static const char *const BL_MAP[] = {"Off", "1", "10", "25", "50", "75", "100", ""};
static const uint16_t CPU_VALUES[] = {80, 160, 240};
static const char *const CPU_MAP[] = {"80", "160", "240", ""};
static const uint8_t PCLK_VALUES[] = {7, 10, 14};
static const char *const PCLK_MAP[] = {"7", "10", "14", ""};
static const char *const SCREEN_MAP[] = {"Static", "Animate", ""};
static const char *const WIFI_MAP[] = {"Off", "No PS", "Modem sleep", "Max PS (DTIM3)", ""};
static const pt_test_t TEST_IDS[] = {
    PT_TEST_BACKLIGHT_OFF, PT_TEST_PANEL_OFF, PT_TEST_TOUCH_SLEEP, PT_TEST_LIGHT_SLEEP,
    PT_TEST_WARM_STANDBY, PT_TEST_DEEP_SLEEP, PT_TEST_DEEP_SLEEP_PARKED, PT_TEST_WAKE_LATENCY,
};
static const char *const TEST_MAP[] = {
    "Backlight off", "Panel off", "+ Touch sleep", "Light sleep", "\n",
    "Warm standby", "Deep sleep", "Deep, pins parked", "Wake latency", "",
};
static const uint16_t DURATION_VALUES[] = {30, 60, 120, 300};
static const char *const DURATION_MAP[] = {"30 s", "60 s", "2 min", "5 min", ""};

static lv_obj_t *s_bl, *s_cpu, *s_pclk, *s_screen, *s_wifi, *s_duration;
static lv_obj_t *s_state_label, *s_boot_label, *s_live_label, *s_top_battery;
static lv_obj_t *s_overlay, *s_overlay_title, *s_overlay_body, *s_overlay_count;
static bool s_overlay_aborts;
static lv_timer_t *s_anim_timer;
static uint16_t s_hue;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COLOR_CARD, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_set_scrollable(c, false);
    return c;
}

static lv_obj_t *segmented(lv_obj_t *parent, const char *caption, const char *const *map, int y, int w,
                           lv_event_cb_t cb, bool checkable)
{
    lv_obj_t *cap = label(parent, &lv_font_montserrat_14, COLOR_MUTED, caption);
    lv_obj_set_pos(cap, RIGHT_X, y);
    lv_obj_t *m = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(m, map);
    lv_obj_set_pos(m, RIGHT_X, y + 20);
    lv_obj_set_size(m, w, 40);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_gap(m, 6, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_14, 0);
    /* Fire once, on release: a long press must not start a test twice. */
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    if (checkable) {
        lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
        lv_buttonmatrix_set_one_checked(m, true);
    }
    lv_obj_add_event_cb(m, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return m;
}

static void check_only(lv_obj_t *m, int idx)
{
    lv_buttonmatrix_clear_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKED);
    if (idx >= 0) {
        lv_buttonmatrix_set_button_ctrl(m, idx, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
}

#define INDEX_OF(arr, v)                                       \
    ({                                                         \
        int _i = -1;                                           \
        for (int _k = 0; _k < (int)(sizeof(arr) / sizeof(arr[0])); _k++) \
            if ((arr)[_k] == (v)) { _i = _k; break; }          \
        _i;                                                    \
    })

static const char *wifi_mode_text(net_mode_t m)
{
    switch (m) {
    case NET_ON_NO_PS: return "Wi-Fi on, no power save";
    case NET_ON_MIN_PS: return "Wi-Fi on, modem sleep";
    case NET_ON_MAX_PS: return "Wi-Fi on, max PS (DTIM3)";
    default: return "Wi-Fi off";
    }
}

void ui_sync_controls(void)
{
    const pt_settings_t *s = runner_settings();
    check_only(s_bl, INDEX_OF(BL_VALUES, s->backlight_pct));
    check_only(s_cpu, INDEX_OF(CPU_VALUES, s->cpu_mhz));
    check_only(s_pclk, INDEX_OF(PCLK_VALUES, s->pclk_mhz));
    check_only(s_screen, s->animate ? 1 : 0);
    check_only(s_wifi, (int)s->wifi);
    check_only(s_duration, INDEX_OF(DURATION_VALUES, runner_duration_s()));

    char bl[16];
    if (s->backlight_pct) {
        snprintf(bl, sizeof(bl), "%u%%", s->backlight_pct);
    } else {
        snprintf(bl, sizeof(bl), "off");
    }
    lv_label_set_text_fmt(s_state_label, "Backlight %s\nCPU %u MHz\nPixel clock %u MHz\n%s\n%s", bl, s->cpu_mhz,
                          s->pclk_mhz, s->animate ? "Full-screen animation" : "Static screen",
                          wifi_mode_text(s->wifi));
}

/* Copy the current settings, let `edit` change one field, apply. */
static void apply_edit(void (*edit)(pt_settings_t *, uint32_t), uint32_t idx)
{
    if (runner_busy()) {
        ui_sync_controls();
        return;
    }
    pt_settings_t s = *runner_settings();
    edit(&s, idx);
    runner_apply(&s);
}

static void edit_bl(pt_settings_t *s, uint32_t i) { s->backlight_pct = BL_VALUES[i]; }
static void edit_cpu(pt_settings_t *s, uint32_t i) { s->cpu_mhz = CPU_VALUES[i]; }
static void edit_pclk(pt_settings_t *s, uint32_t i) { s->pclk_mhz = PCLK_VALUES[i]; }
static void edit_screen(pt_settings_t *s, uint32_t i) { s->animate = i == 1; }
static void edit_wifi(pt_settings_t *s, uint32_t i) { s->wifi = (net_mode_t)i; }

#define SETTING_CB(name, edit_fn)                                              \
    static void name(lv_event_t *e)                                            \
    {                                                                          \
        uint32_t i = lv_buttonmatrix_get_selected_button(lv_event_get_target(e)); \
        if (i != LV_BUTTONMATRIX_BUTTON_NONE) apply_edit(edit_fn, i);          \
    }
SETTING_CB(on_bl, edit_bl)
SETTING_CB(on_cpu, edit_cpu)
SETTING_CB(on_pclk, edit_pclk)
SETTING_CB(on_screen, edit_screen)
SETTING_CB(on_wifi, edit_wifi)

static void on_duration(lv_event_t *e)
{
    uint32_t i = lv_buttonmatrix_get_selected_button(lv_event_get_target(e));
    if (i != LV_BUTTONMATRIX_BUTTON_NONE) {
        runner_set_duration_s(DURATION_VALUES[i]);
    }
}

static void on_test(lv_event_t *e)
{
    uint32_t i = lv_buttonmatrix_get_selected_button(lv_event_get_target(e));
    if (i < sizeof(TEST_IDS) / sizeof(TEST_IDS[0])) {
        runner_start_test(TEST_IDS[i]);
    }
}

static void on_sequence(lv_event_t *e)
{
    runner_start_sequence();
}

static void on_overlay_click(lv_event_t *e)
{
    if (s_overlay_aborts) {
        runner_abort();
    }
}

static void anim_tick(lv_timer_t *t)
{
    /* Restyling the screen invalidates all of it: a full 1024x600 redraw
     * every refresh period, the worst case for CPU and PSRAM traffic. */
    s_hue = (s_hue + 5) % 360;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hsv_to_rgb(s_hue, 60, 25), 0);
}

void ui_set_animate(bool on)
{
    if (on && !s_anim_timer) {
        s_anim_timer = lv_timer_create(anim_tick, 16, NULL);
    } else if (!on && s_anim_timer) {
        lv_timer_delete(s_anim_timer);
        s_anim_timer = NULL;
        lv_obj_set_style_bg_color(lv_screen_active(), COLOR_BG, 0);
    }
}

static void live_tick(lv_timer_t *t)
{
    uint16_t mv = board_battery_mv();
    lv_label_set_text_fmt(s_top_battery, LV_SYMBOL_BATTERY_FULL "  %u.%02u V", mv / 1000, (mv % 1000) / 10);

    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    char wifi[64];
    if (!net_available()) {
        snprintf(wifi, sizeof(wifi), "Wi-Fi: no SSID set");
    } else if (net_get_mode() == NET_OFF) {
        snprintf(wifi, sizeof(wifi), "Wi-Fi: off");
    } else if (net_connected()) {
        snprintf(wifi, sizeof(wifi), "Wi-Fi: up in %d ms%s, %d dBm", net_connect_ms(),
                 net_used_fast_connect() ? " (fast)" : "", net_rssi());
    } else {
        snprintf(wifi, sizeof(wifi), "Wi-Fi: connecting...");
    }
    lv_label_set_text_fmt(s_live_label, "Battery %u mV\nUptime %02lu:%02lu:%02lu\n%s", mv,
                          (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), (unsigned long)(up % 60),
                          wifi);
}

static void create_overlay(void)
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_overlay, 24, 0);
    lv_obj_add_event_cb(s_overlay, on_overlay_click, LV_EVENT_CLICKED, NULL);
    s_overlay_title = label(s_overlay, &lv_font_montserrat_28, COLOR_TEXT, "");
    s_overlay_body = label(s_overlay, &lv_font_montserrat_20, COLOR_MUTED, "");
    lv_obj_set_style_text_align(s_overlay_body, LV_TEXT_ALIGN_CENTER, 0);
    s_overlay_count = label(s_overlay, &lv_font_montserrat_40, COLOR_TEXT, "");
    lv_obj_set_hidden(s_overlay, true);
}

void ui_show_overlay(const char *title, const char *body, bool tap_aborts)
{
    lv_label_set_text(s_overlay_title, title);
    lv_label_set_text(s_overlay_body, body);
    s_overlay_aborts = tap_aborts;
    lv_obj_set_hidden(s_overlay, false);
}

void ui_set_overlay_countdown(int seconds)
{
    if (seconds < 0) {
        lv_obj_set_hidden(s_overlay_count, true);
    } else {
        lv_obj_set_hidden(s_overlay_count, false);
        lv_label_set_text_fmt(s_overlay_count, "%d s", seconds);
    }
}

void ui_hide_overlay(void)
{
    lv_obj_set_hidden(s_overlay, true);
}

void ui_set_boot_note(const char *text)
{
    lv_label_set_text(s_boot_label, text);
}

void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *title = label(scr, &lv_font_montserrat_20, COLOR_TEXT, "Hubmote  /  Power Test");
    lv_obj_set_pos(title, 16, 16);
    s_top_battery = label(scr, &lv_font_montserrat_20, COLOR_TEXT, "");
    lv_obj_align(s_top_battery, LV_ALIGN_TOP_RIGHT, -16, 16);

    /* Left: what is being measured right now, plus this boot's facts. */
    lv_obj_t *left = card(scr, 12, 60, 336, 528);
    label(left, &lv_font_montserrat_14, COLOR_MUTED, "MEASURING NOW");
    s_state_label = label(left, &lv_font_montserrat_20, COLOR_TEXT, "");
    lv_obj_set_pos(s_state_label, 0, 24);
    lv_obj_t *cap = label(left, &lv_font_montserrat_14, COLOR_MUTED, "THIS BOOT");
    lv_obj_set_pos(cap, 0, 182);
    s_boot_label = label(left, &lv_font_montserrat_14, COLOR_TEXT, "");
    lv_obj_set_pos(s_boot_label, 0, 204);
    lv_obj_set_width(s_boot_label, 304);
    cap = label(left, &lv_font_montserrat_14, COLOR_MUTED, "LIVE");
    lv_obj_set_pos(cap, 0, 312);
    s_live_label = label(left, &lv_font_montserrat_20, COLOR_TEXT, "");
    lv_obj_set_pos(s_live_label, 0, 334);
    lv_obj_t *hint = label(left, &lv_font_montserrat_14, COLOR_MUTED,
                           "Change one setting, wait about 10 s\nfor the meter to settle, note it down.");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Right: one row per measurement axis. */
    int y = 60;
    s_bl = segmented(scr, "BACKLIGHT %", BL_MAP, y, RIGHT_W, on_bl, true);
    s_cpu = segmented(scr, "CPU MHz (fixed, no DFS)", CPU_MAP, y += ROW_PITCH, RIGHT_W, on_cpu, true);
    s_pclk = segmented(scr, "PIXEL CLOCK MHz (14 = the remote's value)", PCLK_MAP, y += ROW_PITCH, RIGHT_W,
                       on_pclk, true);
    s_screen = segmented(scr, "SCREEN LOAD", SCREEN_MAP, y += ROW_PITCH, RIGHT_W, on_screen, true);
    s_wifi = segmented(scr, net_available() ? "WI-FI" : "WI-FI (no SSID set: run menuconfig)", WIFI_MAP,
                       y += ROW_PITCH, RIGHT_W, on_wifi, true);
    if (!net_available()) {
        for (int i = 1; i < NET_MODE_COUNT; i++) {
            lv_buttonmatrix_set_button_ctrl(s_wifi, i, LV_BUTTONMATRIX_CTRL_DISABLED);
        }
    }

    y += ROW_PITCH;
    lv_obj_t *tests = segmented(scr, "SCREEN-OFF TESTS  (board reboots when each ends; RESET aborts)", TEST_MAP,
                                y, RIGHT_W, on_test, false);
    lv_obj_set_height(tests, 86);
    if (!net_available()) {
        lv_buttonmatrix_set_button_ctrl(tests, 4, LV_BUTTONMATRIX_CTRL_DISABLED);
    }

    y += 116;
    s_duration = segmented(scr, "SCREEN-OFF DURATION", DURATION_MAP, y, 380, on_duration, true);
    lv_obj_t *seq = lv_button_create(scr);
    lv_obj_set_pos(seq, RIGHT_X + 396, y + 20);
    lv_obj_set_size(seq, RIGHT_W - 396, 40);
    lv_obj_add_event_cb(seq, on_sequence, LV_EVENT_CLICKED, NULL);
    lv_obj_t *seq_label = label(seq, &lv_font_montserrat_14, COLOR_TEXT, LV_SYMBOL_PLAY "  Run full sequence");
    lv_obj_center(seq_label);

    create_overlay();
    ui_sync_controls();
    live_tick(NULL);
    lv_timer_create(live_tick, 1000, NULL);
}
