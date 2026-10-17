/*
 * Hubmote's UI on a PC: the real components/ui and the Weather module's
 * screens, drawn by LVGL into memory at SIM_W x SIM_H, fed with sample
 * data the way the controller feeds them on the remote, and saved as one
 * BMP per screen. sim/run_sim.py builds it for each screen size and turns
 * the BMPs into PNGs.
 *
 *     hubmote_sim <out_dir> [theme]     (theme: midnight, daylight, ...)
 *
 * Time is simulated (33 ms per frame), so the same build always draws the
 * same pictures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"
#include "ui_internal.h"

void sim_weather_load(void);
void hub_glance_changed(void);

static uint32_t s_ms;
static uint16_t *s_frame;
static const char *s_out;
static int s_shots;

static uint32_t tick(void)
{
    return s_ms;
}

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    (void)px;
    lv_display_flush_ready(disp);
}

/* Lets timers and animations run for ms of simulated time. */
static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 33) {
        s_ms += 33;
        lv_timer_handler();
    }
}

static void put_le(uint8_t *p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

/* The frame as a 24-bit BMP, <out>/<name>.bmp. */
static void shot(const char *name)
{
    run(600);
    lv_refr_now(NULL);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.bmp", s_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "sim: can't write %s\n", path);
        exit(1);
    }
    int row = (SIM_W * 3 + 3) & ~3;
    uint8_t hdr[54] = {'B', 'M'};
    put_le(hdr + 2, (uint32_t)(54 + row * SIM_H), 4);
    put_le(hdr + 10, 54, 4);
    put_le(hdr + 14, 40, 4);
    put_le(hdr + 18, SIM_W, 4);
    put_le(hdr + 22, SIM_H, 4);
    put_le(hdr + 26, 1, 2);
    put_le(hdr + 28, 24, 2);
    put_le(hdr + 34, (uint32_t)(row * SIM_H), 4);
    fwrite(hdr, 1, sizeof(hdr), f);
    uint8_t *line = calloc(1, (size_t)row);
    for (int y = SIM_H - 1; y >= 0; y--) {
        for (int x = 0; x < SIM_W; x++) {
            uint16_t c = s_frame[y * SIM_W + x];
            line[3 * x + 0] = (uint8_t)((c & 0x1F) << 3 | (c & 0x1F) >> 2);
            line[3 * x + 1] = (uint8_t)((c >> 5 & 0x3F) << 2 | (c >> 5 & 0x3F) >> 4);
            line[3 * x + 2] = (uint8_t)((c >> 11) << 3 | (c >> 11) >> 2);
        }
        fwrite(line, 1, (size_t)row, f);
    }
    free(line);
    fclose(f);
    s_shots++;
}

static void on_action(const ui_action_t *a, void *ctx)
{
    (void)a;
    (void)ctx;
}

/* --- Sample data: what a set-up remote shows (no real names) --------------------- */

static void sample_data(void)
{
    ui_set_tv_name("Google TV Streamer");
    ui_set_link(UI_LINK_CONNECTED);
    ui_set_wifi(true);
    ui_set_wifi_network("Home network");
    ui_set_battery(76, false);
    ui_set_hostname("hubmote.local");
    ui_set_build("0.1.0", "Oct 16 2026", "12:00");
    ui_set_update(UI_UPDATE_CURRENT, "0.1.0", 0);
    ui_about_t about = {.slot = "ota_0", .address = "192.168.1.40", .rssi = -52, .uptime_min = 135,
                        .internal_kb = 74, .psram_kb = 3300};
    ui_set_about(&about);
    ui_set_companion(UI_COMPANION_ON);
    ui_set_companion_version("0.6", UI_COMPANION_UPDATE_NONE, "", false);
    ui_set_brightness(80);
    ui_timeouts_t t = {.dim_s = 30, .off_s = 60, .dim_charging_s = 300, .off_charging_s = 1800, .sleep_h = 12};
    ui_set_screen_timeouts(&t);
    static const ui_app_t apps[] = {
        {"com.netflix.ninja", "Netflix", NULL, NULL},
        {"com.google.android.youtube.tv", "YouTube", NULL, NULL},
        {"com.amazon.amazonvideo.livingroom", "Prime Video", NULL, NULL},
        {"com.plexapp.android", "Plex", NULL, NULL},
        {"com.spotify.tv.android", "Spotify", NULL, NULL},
        {"com.disney.disneyplus", "Disney+", NULL, NULL},
        {"tv.pluto.android", "Pluto TV", NULL, NULL},
        {"com.tubitv", "Tubi", NULL, NULL},
    };
    ui_set_apps(apps, sizeof(apps) / sizeof(apps[0]));
    static const char *const keys[] = {"power_on", "power_off", "hdmi1", "hdmi2", "hdmi3", "hdmi4", "source",
                                       "ch_up", "ch_down", "vol_up", "vol_down", "mute", "home", "menu"};
    ui_set_ir_tv("LG TV", "Built-in codes, set 2", keys, sizeof(keys) / sizeof(keys[0]), false);
    static const char *const brands[] = {"Hisense", "LG", "Philips", "Samsung", "Sharp", "Sony", "TCL", "Toshiba",
                                         "Vizio"};
    ui_set_tv_brands(brands, sizeof(brands) / sizeof(brands[0]));
    static const char *const remotes[] = {"Soundbar", "Projector"};
    ui_set_ir_remotes(remotes, 2);
    static const char *const inputs[] = {"Streamer", "Console", "PC", ""};
    static const bool learned[] = {true, true, true, false};
    ui_set_inputs(inputs, learned);
    ui_set_volume(12, 100, false);
    hub_glance_changed(); /* Weather's glance in the top bar, as when its data arrives */
    ui_settings_modules_changed(); /* as hub_modules_init() does on the remote */
}

/* --- The screens ---------------------------------------------------------------- */

static void screens(void)
{
    /* First run */
    ui_setup_busy("Connecting to Wi-Fi", "Home network");
    shot("setup_busy");
    static const ui_wifi_ap_t aps[] = {
        {"Home network", -48, true}, {"Guest", -61, true}, {"Cafe", -77, false}, {"Upstairs", -83, true}};
    ui_setup_wifi(NULL);
    ui_set_wifi_networks(aps, sizeof(aps) / sizeof(aps[0]));
    shot("setup_wifi");
    static const ui_tv_t tvs[] = {{"Living room", "192.168.1.20"}, {"Bedroom", "192.168.1.21"}};
    ui_setup_tvs(tvs, 2);
    shot("setup_tvs");
    ui_setup_code("Living room");
    shot("setup_code");
    ui_setup_error("Can't reach your TV",
                   "It may be off, or its address may have changed. Try again searches the network.");
    shot("setup_error");

    /* The Streamer */
    ui_show_main();
    ui_set_app("com.google.android.apps.tv.launcherx");
    shot("tv_home");
    ui_set_app("com.netflix.ninja");
    shot("app_netflix");
    ui_set_app("com.google.android.youtube.tv");
    ui_set_media("com.google.android.youtube.tv", "How the Moon formed", "Science Channel", true);
    shot("app_youtube");
    ui_ime_show("", "Search");
    shot("keyboard");
    ui_kb_close();
    ui_set_app("com.google.android.apps.tv.launcherx");
    ui_switcher_open();
    shot("switcher");
    ui_switcher_close();
    ui_notify(ICON_CHECK, "Companion app paired");
    shot("toast");
    run(4000);

    /* Home tab and Weather */
    ui_show_view(VIEW_DASH);
    shot("home_tab");
    ui_module_open("weather");
    shot("weather");

    /* IR */
    ui_show_ir_tv();
    shot("ir_tv");
    static const ui_ir_button_t buttons[] = {{"Power", 0}, {"Input", 1}, {"Vol +", 2}, {"Vol -", 3},
                                             {"Mute", 4},  {"Bass", 5},  {"Movie", 8}, {"Music", 9}};
    ui_show_learned_remote(0, "Soundbar", buttons, sizeof(buttons) / sizeof(buttons[0]));
    shot("learned_remote");
    ui_show_inputs();
    shot("inputs");
    ui_learn_open(-1);
    ui_show_view(VIEW_LEARN);
    shot("learn");
    ui_tv_setup_open();
    shot("add_tv");
    static const char *const test_keys[] = {"power", "power", "vol_up"};
    ui_tv_setup_sets("LG", test_keys, 3);
    shot("add_tv_sets");
    ui_tv_setup_open_keys();
    shot("tv_keys");

    /* Settings */
    static const char *const sections[SEC_COUNT] = {"remotes", "wifi", "screen", "appearance", "modules",
                                                     "about"};
    for (int i = 0; i < SEC_COUNT; i++) {
        ui_settings_show_section((ui_section_t)i);
        char name[32];
        snprintf(name, sizeof(name), "settings_%s", sections[i]);
        shot(name);
    }

    /* Dialogs: the time-zone picker, and a message with buttons */
    ui_settings_show_section(SEC_MODULES);
    ui_settings_pick_time_zone(NULL);
    shot("time_zone");
    ui_settings_set_time_zone("America/New_York");
    ui_settings_pick_time_zone("America");
    shot("time_zone_americas");
    ui_modal_close();
    ui_settings_show_section(SEC_MODULES);
    shot("settings_modules_zone");
    ui_settings_set_time_zone("");
    static const char *const dialog_buttons[] = {"Cancel", "Turn off"};
    ui_modal("Turn off the remote?", "Touch the screen or press BOOT to turn it on again.", dialog_buttons, 2, NULL, NULL);
    shot("modal");
    ui_modal_close();

    /* A plain board (the 5B, the 4.3B): no pins for IR, a backlight that is
     * only on or off, no battery reading */
    ui_set_ir_caps(false, false);
    ui_set_board_caps(false, false);
    ui_settings_show_section(SEC_REMOTES);
    shot("plain_settings_remotes");
    ui_settings_show_section(SEC_SCREEN);
    shot("plain_settings_screen");
    ui_show_main();
    ui_set_app("com.google.android.apps.tv.launcherx");
    ui_switcher_open();
    shot("plain_switcher");
    ui_switcher_close();
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: hubmote_sim <out_dir> [theme]\n");
        return 2;
    }
    s_out = argv[1];

    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *disp = lv_display_create(SIM_W, SIM_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    size_t bytes = (size_t)SIM_W * SIM_H * 2;
    s_frame = malloc(bytes);
    lv_display_set_buffers(disp, s_frame, NULL, (uint32_t)bytes, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush);

    sim_weather_load();
    if (argc > 2) {
        ui_set_theme(argv[2]);
    }
    ui_init(on_action, NULL);
    sample_data();
    screens();
    printf("%d screens, %dx%d\n", s_shots, SIM_W, SIM_H);
    return 0;
}
