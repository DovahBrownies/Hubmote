/*
 * Settings (VIEW_SETTINGS): sections on the left, the chosen one on the
 * right.
 *  - Remotes: the Google TV (pair again, find TVs, TV volume via the
 *    Streamer or IR), the companion app (pair, update), IR devices,
 *    learning.
 *  - Wi-Fi: the network in use; choose another.
 *  - Screen: brightness, Rotate 180°, and when the screen dims, goes dark
 *    and the remote turns itself off.
 *  - Appearance: the theme (applied with a restart).
 *  - Modules: the time zone, each module on or off, and its settings.
 *  - About: version, build, network, uptime, memory.
 * Turn off remote and Done sit under the sections.
 */
#include <stdio.h>
#include <string.h>
#include "hub_module.h"
#include "hub_time.h"
#include "ui_internal.h"

#define NAV_W   UI_S(248)
#define PANE_W  (UI_WIDE_W - NAV_W - UI_GAP)
#define ROW_H   UI_S(64)
#define CHOICES 5


static const struct {
    const char *icon;
    const char *name;
} k_sections[SEC_COUNT] = {
    [SEC_REMOTES] = {ICON_REMOTE, "Remotes"},
    [SEC_WIFI] = {ICON_WIFI, "Wi-Fi"},
    [SEC_SCREEN] = {ICON_W_SUN, "Screen"},
    [SEC_APPEARANCE] = {ICON_PALETTE, "Appearance"},
    [SEC_MODULES] = {ICON_GRID, "Modules"},
    [SEC_ABOUT] = {ICON_INFO, "About"},
};

static ui_section_t s_section;
static lv_obj_t *s_nav[SEC_COUNT];
static lv_obj_t *s_nav_badge[SEC_COUNT]; /* an update waits in this section */
static lv_obj_t *s_page[SEC_COUNT];

/* Remotes */
static lv_obj_t *s_tv_name;
static lv_obj_t *s_tv_dot;
static lv_obj_t *s_tv_status;
static lv_obj_t *s_companion_btn;
static lv_obj_t *s_comp_update_btn;
static ui_link_t s_tv_link;
static ui_companion_t s_companion;
static char s_comp_version[16]; /* on the TV; "" unknown */
static char s_comp_next[16];    /* in the release */
static ui_companion_update_t s_comp_update;
static bool s_comp_can_install;
static lv_obj_t *s_ir_head; /* "IR devices", and what follows it */
static lv_obj_t *s_ir_list;
static lv_obj_t *s_learn_remote;
static char s_remote_names[UI_IR_MAX_REMOTES][UI_IR_NAME_MAX];
static size_t s_remote_count;

/* Screen */
static lv_obj_t *s_turn_switch;
static lv_obj_t *s_bright_slider;
static lv_obj_t *s_bright_value;

typedef struct {
    const char *label;
    int value;
} choice_t;

typedef struct {
    const choice_t *choices;
    ui_action_type_t action;
    lv_obj_t *btn[CHOICES];
} choice_group_t;

static const choice_t k_dim[CHOICES] = {{"15 s", 15}, {"30 s", 30}, {"1 min", 60}, {"2 min", 120}, {"5 min", 300}};
static const choice_t k_off[CHOICES] = {{"30 s", 30}, {"1 min", 60}, {"2 min", 120}, {"5 min", 300}, {"10 min", 600}};
static const choice_t k_sleep[CHOICES] = {{"1 h", 1}, {"4 h", 4}, {"12 h", 12}, {"24 h", 24}, {"Never", 0}};
static const choice_t k_dim_charging[CHOICES] = {
    {"1 min", 60}, {"5 min", 300}, {"15 min", 900}, {"30 min", 1800}, {"Never", 0}};
static const choice_t k_off_charging[CHOICES] = {
    {"5 min", 300}, {"15 min", 900}, {"30 min", 1800}, {"1 h", 3600}, {"Never", 0}};
static choice_group_t s_dim = {.choices = k_dim, .action = UI_ACT_DIM_AFTER};
static choice_group_t s_off = {.choices = k_off, .action = UI_ACT_SCREEN_OFF_AFTER};
static choice_group_t s_sleep = {.choices = k_sleep, .action = UI_ACT_SLEEP_AFTER};

/* Brightness (its row, slider and divider) and Dim after: hidden where the
 * backlight is only on or off. The On battery / Charging switch: hidden
 * where the battery can't be read. */
static lv_obj_t *s_dimming[4];
static lv_obj_t *s_power_row;
/* The Dim and Screen off rows show either the times on battery (0) or
 * while charging (1); the On battery / Charging switch picks. */
static int s_power_mode;
static int s_dim_value[2];
static int s_off_value[2];
static lv_obj_t *s_power_btn[2];

/* Appearance */
#define THEMES_MAX 8
/* Three across the pane (inside its UI_S(20) padding), UI_S(12) apart. */
#define TILE_W     LV_MIN(UI_S(222), (PANE_W - 2 * UI_S(20) - 2 * UI_S(12)) / 3)
static lv_obj_t *s_theme_tile[THEMES_MAX];
static lv_obj_t *s_theme_apply;
static size_t s_theme_pending;

/* Modules */
static lv_obj_t *s_mod_list;
static lv_obj_t *s_mod_none;
static lv_obj_t *s_mod_switch[HUB_MODULES_MAX];
static lv_obj_t *s_mod_settings[HUB_MODULES_MAX]; /* its settings, shown while it is on */
static const hub_module_t *s_mod_of[HUB_MODULES_MAX];
static size_t s_mod_count;
/* Each module setting on screen, with what shows its value. */
#define MOD_SETTINGS_MAX 32
typedef struct {
    const hub_module_t *module;
    const hub_setting_t *setting;
    lv_obj_t *control; /* SWITCH: the switch; NUMBER, TEXT, CHOICE as a list: the value's label */
    lv_obj_t *seg[3];  /* CHOICE side by side: its buttons */
    int32_t value;
} mod_setting_t;
static mod_setting_t s_mset[MOD_SETTINGS_MAX];
static size_t s_mset_count;
static mod_setting_t *s_mset_editing; /* the TEXT setting on the keyboard */

/* About */
enum { AB_VERSION, AB_BUILT, AB_SLOT, AB_ADDRESS, AB_NAME, AB_SIGNAL, AB_UPTIME, AB_MEMORY, AB_TOKEN, AB_COUNT };
static const char *const k_about[AB_COUNT] = {
    "Version", "Built", "Update slot", "Address", "Name", "Wi-Fi signal", "Uptime", "Free memory", "Update token",
};
static char s_token[96];
/* Updates (the Version row): state, the release's version, progress. */
static ui_update_t s_upd;
static char s_upd_version[32];
static int s_upd_percent;
static lv_obj_t *s_upd_btn;
static lv_obj_t *s_upd_spinner;
static bool s_token_shown;
static lv_obj_t *s_token_btn;
static lv_obj_t *s_token_row[2]; /* the row and the line above it: hidden without a token */
static lv_obj_t *s_about[AB_COUNT];
static char s_hostname[40];
static char s_version[32]; /* version.txt's, as the app was built */
static ui_about_t s_about_info;
static char s_built_date[16]; /* "Oct 13 2026" */
static char s_built_time[8];  /* "23:10" */
static char s_version_text[64];

/* --- Shared pieces ------------------------------------------------------------ */

static lv_obj_t *page_box(lv_obj_t *parent, int32_t gap)
{
    lv_obj_t *p = ui_box(parent);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    ui_flex(p, LV_FLEX_FLOW_COLUMN, gap);
    return p;
}

static lv_obj_t *subhead(lv_obj_t *parent, const char *text)
{
    return ui_label(parent, &font_sora_13m, COL_TEXT2, text);
}

static lv_obj_t *divider(lv_obj_t *parent)
{
    lv_obj_t *line = ui_box(parent);
    lv_obj_set_size(line, LV_PCT(100), UI_S(1));
    lv_obj_set_style_bg_color(line, COL_LINE, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    return line;
}

/* A row with a title on the left; the caller adds the control on the right. */
static lv_obj_t *setting_row(lv_obj_t *parent, const char *title)
{
    lv_obj_t *r = ui_box(parent);
    lv_obj_set_size(r, LV_PCT(100), ROW_H);
    ui_flex(r, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(r, &font_sora_15m, COL_TEXT, title);
    return r;
}

static lv_obj_t *icon_box(lv_obj_t *parent, const char *icon, lv_color_t color)
{
    lv_obj_t *box = ui_box(parent);
    lv_obj_set_size(box, UI_S(40), UI_S(40));
    lv_obj_set_style_radius(box, ui_radius(12), 0);
    lv_obj_set_style_bg_color(box, COL_SURFACE3, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_center(ui_label(box, &font_icons_20, color, icon));
    return box;
}

/* --- Sections ------------------------------------------------------------------ */

static void about_refresh(void);
static void module_settings_refresh(void);

static void select_section(ui_section_t sec)
{
    s_section = sec;
    for (int i = 0; i < SEC_COUNT; i++) {
        bool on = i == (int)sec;
        lv_obj_set_style_bg_color(s_nav[i], on ? COL_SURFACE3 : COL_SURFACE1, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_nav[i], 0), on ? COL_ACCENT_TEXT : COL_TEXT2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_nav[i], 1), on ? COL_TEXT : COL_TEXT2, 0);
        lv_obj_set_hidden(s_page[i], !on);
    }
    if (sec != SEC_ABOUT) {
        s_token_shown = false; /* shown only while looking at it */
    }
    if (sec == SEC_ABOUT) {
        about_refresh();
    }
    if (sec == SEC_MODULES) {
        module_settings_refresh(); /* a module's own screen may have changed one */
    }
}

static void on_nav(lv_event_t *e)
{
    ui_section_t sec = (ui_section_t)(intptr_t)lv_event_get_user_data(e);
    select_section(sec);
    if (sec == SEC_WIFI) {
        ui_wifi_scan(); /* fresh list each time the section opens */
    }
}

static void on_done(lv_event_t *e)
{
    ui_remote_return();
}

void ui_settings_shown(void)
{
    s_token_shown = false; /* hidden again each time Settings opens */
    select_section(s_section);
}

void ui_settings_show_section(ui_section_t section)
{
    s_section = section;
    ui_show_view(VIEW_SETTINGS);
}

void ui_settings_show_remotes(void)
{
    ui_settings_show_section(SEC_REMOTES);
}

void ui_settings_show_appearance(void)
{
    ui_settings_show_section(SEC_APPEARANCE);
}

/* --- Remotes ------------------------------------------------------------------- */

static void on_tv_row(lv_event_t *e)
{
    ui_return_to_context();
}

static void on_irtv(lv_event_t *e)
{
    if (ui_irtv_configured()) {
        ui_irtv_show();
    } else {
        ui_tv_setup_open();
    }
}

void ui_settings_open_remote(int index)
{
    ui_ir_remote_set_index(index);
    ui_action_t a = {.type = UI_ACT_OPEN_REMOTE, .index = index};
    ui_emit(&a);
}

size_t ui_settings_remote_count(void)
{
    return s_remote_count;
}

const char *ui_settings_remote_name(size_t index)
{
    return index < s_remote_count ? s_remote_names[index] : "";
}

static void on_remote(lv_event_t *e)
{
    ui_settings_open_remote((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_learn(lv_event_t *e)
{
    ui_learn_open(-1);
    ui_show_view(VIEW_LEARN);
}

static lv_obj_t *device_row(lv_obj_t *parent, const char *icon, lv_color_t icon_color, const char *name,
                            const char *detail, const char *tag)
{
    lv_obj_t *row = ui_btn(parent, LV_PCT(100), UI_S(64), 16, COL_SURFACE2);
    lv_obj_set_style_pad_hor(row, UI_S(14), 0);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    icon_box(row, icon, icon_color);
    lv_obj_t *names = ui_box(row);
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(names, 1);
    ui_flex(names, LV_FLEX_FLOW_COLUMN, UI_S(2));
    ui_label(names, &font_sora_15m, COL_TEXT, name);
    ui_label(names, &font_sora_12, COL_TEXT2, detail);
    lv_obj_t *pill = ui_box(row);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(pill, UI_S(10), 0);
    lv_obj_set_style_pad_ver(pill, UI_S(4), 0);
    lv_obj_set_style_radius(pill, ui_radius(LV_RADIUS_CIRCLE), 0);
    lv_obj_set_style_bg_color(pill, COL_IR, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
    ui_label(pill, &font_mono_11, COL_IR, tag);
    ui_label(row, &font_icons_16, COL_TEXT3, ICON_CHEVRON_RIGHT);
    return row;
}

static void rebuild_ir_list(size_t count)
{
    lv_obj_clean(s_ir_list);
    lv_obj_t *tv = ui_irtv_configured()
                       ? device_row(s_ir_list, ICON_TV, COL_IR, ui_irtv_name(), ui_irtv_detail(), "IR")
                       : device_row(s_ir_list, ICON_ADD, COL_IR, "Add a TV", "", "IR");
    lv_obj_add_event_cb(tv, on_irtv, LV_EVENT_CLICKED, NULL);
    for (size_t i = 0; i < count; i++) {
        lv_obj_t *r = device_row(s_ir_list, ICON_IR, COL_IR, s_remote_names[i], "Learned remote", "IR");
        lv_obj_add_event_cb(r, on_remote, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

void ui_settings_set_remotes(const char *const *names, size_t count)
{
    if (count > UI_IR_MAX_REMOTES) {
        count = UI_IR_MAX_REMOTES;
    }
    for (size_t i = 0; i < count; i++) {
        strlcpy(s_remote_names[i], names[i], sizeof(s_remote_names[i]));
    }
    s_remote_count = count;
    rebuild_ir_list(count);
}

/* TV volume: through the Streamer (HDMI-CEC) or the TV's own IR. Shown
 * once there is an IR TV. */
static lv_obj_t *s_vol_row;
static lv_obj_t *s_vol_btn[2];

static void paint_tv_volume(void)
{
    bool ir = ui_irtv_volume();
    for (int i = 0; i < 2; i++) {
        bool on = (i == 1) == ir;
        lv_obj_set_style_bg_color(s_vol_btn[i], on ? COL_SURFACE3 : COL_SURFACE2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_vol_btn[i], 0), on ? COL_ACCENT_TEXT : COL_TEXT2, 0);
    }
    lv_obj_set_hidden(s_vol_row, !ui_irtv_configured() || !ui_ir_can_send());
}

static void on_tv_volume(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_TV_VOLUME, .index = (int)(intptr_t)lv_event_get_user_data(e)};
    ui_emit(&a);
}

static void build_tv_volume(lv_obj_t *page)
{
    s_vol_row = ui_box(page);
    lv_obj_set_size(s_vol_row, LV_PCT(100), UI_S(48));
    ui_flex(s_vol_row, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(s_vol_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(s_vol_row, &font_sora_15m, COL_TEXT, "TV volume");
    lv_obj_t *seg = ui_box(s_vol_row);
    lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(seg, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(seg, ui_radius(12), 0);
    lv_obj_set_style_pad_all(seg, UI_S(3), 0);
    ui_flex(seg, LV_FLEX_FLOW_ROW, UI_S(2));
    static const char *const names[] = {"Google TV", "TV's IR"};
    for (int i = 0; i < 2; i++) {
        s_vol_btn[i] = ui_btn_text(seg, UI_S(110), UI_S(40), 10, COL_SURFACE2, &font_sora_13m, COL_TEXT2, names[i]);
        lv_obj_add_event_cb(s_vol_btn[i], on_tv_volume, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    paint_tv_volume();
}

void ui_settings_tv_changed(void)
{
    rebuild_ir_list(s_remote_count);
    paint_tv_volume();
}

/* A settings page that scrolls as a whole, for those that outgrow the screen. */
static void page_scrolls(lv_obj_t *page)
{
    lv_obj_set_scrollable(page, true);
    lv_obj_set_clickable(page, true); /* a drag anywhere scrolls, not only on a button */
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
}

static void build_remotes(lv_obj_t *page)
{
    page_scrolls(page); /* learned remotes add up */
    subhead(page, "Google TV");
    lv_obj_t *row = ui_btn(page, LV_PCT(100), UI_S(76), 16, COL_SURFACE2);
    lv_obj_add_event_cb(row, on_tv_row, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_hor(row, UI_S(16), 0);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(14));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *icon = ui_box(row);
    lv_obj_set_size(icon, UI_S(44), UI_S(44));
    lv_obj_set_style_radius(icon, ui_radius(12), 0);
    lv_obj_set_style_bg_color(icon, COL_SURFACE3, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_center(ui_label(icon, &font_icons_24, COL_ACCENT_TEXT, ICON_STREAMER));
    lv_obj_t *names = ui_box(row);
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(names, 1);
    ui_flex(names, LV_FLEX_FLOW_COLUMN, UI_S(4));
    s_tv_name = ui_label(names, &font_sora_15m, COL_TEXT, "Google TV Streamer");
    lv_obj_t *status = ui_box(names);
    lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(status, LV_FLEX_FLOW_ROW, UI_S(6));
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_tv_dot = ui_box(status);
    lv_obj_set_size(s_tv_dot, UI_S(8), UI_S(8));
    lv_obj_set_style_radius(s_tv_dot, ui_radius(LV_RADIUS_CIRCLE), 0);
    lv_obj_set_style_bg_opa(s_tv_dot, LV_OPA_COVER, 0);
    s_tv_status = ui_label(status, &font_sora_12, COL_TEXT2, "");

    lv_obj_t *btns = ui_box(page);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(btns, LV_FLEX_FLOW_ROW, UI_GAP);
    lv_obj_t *repair = ui_btn_icon(btns, UI_S(10), UI_S(52), 14, COL_SURFACE2, &font_icons_20, ICON_LINK, COL_TEXT, "Pair again", false);
    lv_obj_set_flex_grow(repair, 1);
    ui_btn_bind_action(repair, UI_ACT_REPAIR, NULL, 0);
    lv_obj_t *rescan = ui_btn_icon(btns, UI_S(10), UI_S(52), 14, COL_SURFACE2, &font_icons_20, ICON_REFRESH, COL_TEXT, "Find TVs", false);
    lv_obj_set_flex_grow(rescan, 1);
    ui_btn_bind_action(rescan, UI_ACT_RESCAN, NULL, 0);
    s_companion_btn = ui_btn_icon(btns, UI_S(10), UI_S(52), 14, COL_ACCENT, &font_icons_20, ICON_LINK, COL_ON_ACCENT,
                                  "Pair companion", false);
    lv_obj_set_flex_grow(s_companion_btn, 1);
    ui_btn_bind_action(s_companion_btn, UI_ACT_COMPANION_PAIR, NULL, 0);
    s_comp_update_btn = ui_btn_icon(btns, UI_S(10), UI_S(52), 14, COL_ACCENT, &font_icons_20, ICON_REFRESH, COL_ON_ACCENT,
                                    "Update companion", false);
    lv_obj_set_flex_grow(s_comp_update_btn, 1);
    ui_btn_bind_action(s_comp_update_btn, UI_ACT_COMPANION_UPDATE, NULL, 0);
    ui_settings_set_tv("Google TV Streamer", UI_LINK_OFFLINE);

    lv_obj_t *ir_head = s_ir_head = subhead(page, "IR devices");
    lv_obj_set_style_pad_top(ir_head, UI_S(6), 0);
    s_ir_list = ui_box(page);
    lv_obj_set_size(s_ir_list, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(s_ir_list, LV_FLEX_FLOW_COLUMN, UI_S(10));
    rebuild_ir_list(0);
    build_tv_volume(page);
    lv_obj_t *add = s_learn_remote = ui_btn_icon(page, LV_PCT(100), UI_S(56), 16, COL_SURFACE1, &font_icons_20,
                                                 ICON_IR, COL_IR, "Learn a remote", false);
    lv_obj_set_style_border_width(add, UI_S(2), 0);
    lv_obj_set_style_border_color(add, COL_IR_LINE, 0);
    lv_obj_add_event_cb(add, on_learn, LV_EVENT_CLICKED, NULL);
}

void ui_settings_ir_caps_changed(void)
{
    lv_obj_set_hidden(s_ir_head, !ui_ir_can_send());
    lv_obj_set_hidden(s_ir_list, !ui_ir_can_send());
    lv_obj_set_hidden(s_learn_remote, !ui_ir_can_learn());
    paint_tv_volume();
}

static void paint_tv_status(void)
{
    static const char *const companion[] = {
        [UI_COMPANION_NONE] = "Connected \xC2\xB7 no companion app",
        [UI_COMPANION_UNPAIRED] = "Connected \xC2\xB7 companion app not paired",
        [UI_COMPANION_ON] = "Connected \xC2\xB7 companion app on",
    };
    static const char *const link[] = {"Offline", "Connecting"};
    bool on = s_tv_link == UI_LINK_CONNECTED && s_companion == UI_COMPANION_ON;
    if (!on) {
        lv_label_set_text(s_tv_status, s_tv_link == UI_LINK_CONNECTED ? companion[s_companion] : link[s_tv_link]);
    } else if (s_comp_update == UI_COMPANION_UPDATE_RUNNING) {
        lv_label_set_text(s_tv_status, "Connected \xC2\xB7 updating companion app");
    } else if (s_comp_update == UI_COMPANION_UPDATE_CONFIRM) {
        lv_label_set_text(s_tv_status, "Connected \xC2\xB7 companion update: confirm on the TV");
    } else if (s_comp_update == UI_COMPANION_UPDATE_FAILED) {
        lv_label_set_text(s_tv_status, "Connected \xC2\xB7 companion update didn't work");
    } else if (s_comp_update == UI_COMPANION_UPDATE_AVAILABLE && s_comp_version[0]) {
        lv_label_set_text_fmt(s_tv_status, "Connected \xC2\xB7 companion app %s \xE2\x86\x92 %s", s_comp_version,
                              s_comp_next);
    } else if (s_comp_version[0]) {
        lv_label_set_text_fmt(s_tv_status, "Connected \xC2\xB7 companion app %s", s_comp_version);
    } else {
        lv_label_set_text(s_tv_status, companion[UI_COMPANION_ON]);
    }
    lv_obj_set_style_bg_color(s_tv_dot, s_tv_link == UI_LINK_CONNECTED ? COL_ACCENT_TEXT : COL_TEXT3, 0);
    lv_obj_set_hidden(s_companion_btn, s_companion != UI_COMPANION_UNPAIRED);
    /* Also while Android's dialog should be up: it may have been closed
     * (Home, or the TV asleep), and a new tap shows it again. */
    bool offer = s_comp_update == UI_COMPANION_UPDATE_AVAILABLE || s_comp_update == UI_COMPANION_UPDATE_FAILED ||
                 s_comp_update == UI_COMPANION_UPDATE_CONFIRM;
    lv_obj_set_hidden(s_comp_update_btn, !(on && s_comp_can_install && offer));
}

void ui_settings_set_tv(const char *name, ui_link_t link)
{
    if (name && name[0]) {
        lv_label_set_text(s_tv_name, name);
    }
    s_tv_link = link;
    if (s_companion_btn) {
        paint_tv_status();
    }
}

void ui_settings_set_companion(ui_companion_t state)
{
    s_companion = state;
    paint_tv_status();
}

void ui_settings_set_companion_version(const char *version, ui_companion_update_t update, const char *next,
                                       bool can_install)
{
    strlcpy(s_comp_version, version ? version : "", sizeof(s_comp_version));
    strlcpy(s_comp_next, next ? next : "", sizeof(s_comp_next));
    s_comp_update = update;
    s_comp_can_install = can_install;
    if (s_comp_update_btn) {
        paint_tv_status();
    }
}

/* --- Wi-Fi --------------------------------------------------------------------- */

static lv_obj_t *s_wifi_name;
static lv_obj_t *s_wifi_dot;
static lv_obj_t *s_wifi_state;

void ui_settings_wifi_changed(void)
{
    if (!s_wifi_name) {
        return;
    }
    const char *ssid = ui_wifi_current();
    bool up = ui_wifi_is_up();
    lv_label_set_text(s_wifi_name, ssid[0] ? ssid : "No network");
    lv_label_set_text(s_wifi_state, up ? "Connected" : ssid[0] ? "Connecting" : "Not set up");
    lv_obj_set_style_bg_color(s_wifi_dot, up ? COL_GOOD : COL_TEXT3, 0);
}

static void build_wifi(lv_obj_t *page)
{
    lv_obj_t *row = ui_box(page);
    lv_obj_set_size(row, LV_PCT(100), UI_S(72));
    lv_obj_set_style_bg_color(row, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_radius(16), 0);
    lv_obj_set_style_pad_hor(row, UI_S(16), 0);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(14));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    icon_box(row, ICON_WIFI, COL_ACCENT_TEXT);
    lv_obj_t *names = ui_box(row);
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(names, 1);
    ui_flex(names, LV_FLEX_FLOW_COLUMN, UI_S(4));
    s_wifi_name = ui_label(names, &font_sora_15m, COL_TEXT, "");
    lv_label_set_long_mode(s_wifi_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_wifi_name, LV_PCT(100));
    lv_obj_t *status = ui_box(names);
    lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex(status, LV_FLEX_FLOW_ROW, UI_S(6));
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_wifi_dot = ui_box(status);
    lv_obj_set_size(s_wifi_dot, UI_S(8), UI_S(8));
    lv_obj_set_style_radius(s_wifi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_wifi_dot, LV_OPA_COVER, 0);
    s_wifi_state = ui_label(status, &font_sora_12, COL_TEXT2, "");
    ui_wifi_picker_create(page, UI_WIFI_SETTINGS, VIEW_SETTINGS);
    ui_settings_wifi_changed();
}

/* --- Screen -------------------------------------------------------------------- */

static lv_obj_t *module_switch(lv_obj_t *parent);

static void on_turn(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_SCREEN_TURN, .index = lv_obj_has_state(s_turn_switch, LV_STATE_CHECKED)};
    ui_emit(&a);
}

void ui_settings_set_turned(bool turned)
{
    lv_obj_set_state(s_turn_switch, LV_STATE_CHECKED, turned);
}

static void on_brightness(lv_event_t *e)
{
    int32_t v = lv_slider_get_value(s_bright_slider);
    lv_label_set_text_fmt(s_bright_value, "%d%%", (int)v);
    ui_action_t a = {.type = UI_ACT_BRIGHTNESS, .index = (int)v};
    ui_emit(&a);
}

void ui_settings_set_brightness(int percent)
{
    lv_slider_set_value(s_bright_slider, percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_bright_value, "%d%%", percent);
}

static void choice_select(choice_group_t *g, int value)
{
    for (int i = 0; i < CHOICES; i++) {
        bool on = g->choices[i].value == value;
        lv_obj_set_style_bg_color(g->btn[i], on ? COL_SURFACE3 : COL_SURFACE2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g->btn[i], 0), on ? COL_ACCENT_TEXT : COL_TEXT2, 0);
    }
}

static void on_choice(lv_event_t *e)
{
    choice_group_t *g = lv_event_get_user_data(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    choice_select(g, g->choices[i].value);
    if (g == &s_dim) {
        s_dim_value[s_power_mode] = g->choices[i].value;
    } else if (g == &s_off) {
        s_off_value[s_power_mode] = g->choices[i].value;
    }
    ui_action_t a = {.type = g->action, .index = g->choices[i].value};
    ui_emit(&a);
}

static lv_obj_t *choice_row(lv_obj_t *page, const char *title, choice_group_t *g)
{
    lv_obj_t *row = setting_row(page, title);
    lv_obj_t *seg = ui_box(row);
    lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(seg, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(seg, ui_radius(12), 0);
    lv_obj_set_style_pad_all(seg, UI_S(3), 0);
    ui_flex(seg, LV_FLEX_FLOW_ROW, UI_S(2));
    for (int i = 0; i < CHOICES; i++) {
        g->btn[i] = ui_btn_text(seg, UI_S(74), UI_S(40), 10, COL_SURFACE2, &font_sora_13m, COL_TEXT2, g->choices[i].label);
        lv_obj_set_user_data(g->btn[i], (void *)(intptr_t)i);
        lv_obj_add_event_cb(g->btn[i], on_choice, LV_EVENT_CLICKED, g);
    }
    return row;
}

static void group_show(choice_group_t *g, const choice_t *choices, ui_action_type_t action, int value)
{
    g->choices = choices;
    g->action = action;
    for (int i = 0; i < CHOICES; i++) {
        lv_label_set_text(lv_obj_get_child(g->btn[i], 0), choices[i].label);
    }
    choice_select(g, value);
}

static void paint_power_mode(void)
{
    bool charging = s_power_mode == 1;
    group_show(&s_dim, charging ? k_dim_charging : k_dim, charging ? UI_ACT_DIM_AFTER_CHARGING : UI_ACT_DIM_AFTER,
               s_dim_value[s_power_mode]);
    group_show(&s_off, charging ? k_off_charging : k_off,
               charging ? UI_ACT_SCREEN_OFF_AFTER_CHARGING : UI_ACT_SCREEN_OFF_AFTER, s_off_value[s_power_mode]);
    for (int i = 0; i < 2; i++) {
        bool on = i == s_power_mode;
        lv_obj_set_style_bg_color(s_power_btn[i], on ? COL_SURFACE3 : COL_SURFACE2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_power_btn[i], 0), on ? COL_ACCENT_TEXT : COL_TEXT2, 0);
    }
}

static void on_power_mode(lv_event_t *e)
{
    s_power_mode = (int)(intptr_t)lv_event_get_user_data(e);
    paint_power_mode();
}

static void power_row(lv_obj_t *page)
{
    lv_obj_t *row = s_power_row = setting_row(page, "Timeouts");
    lv_obj_t *seg = ui_box(row);
    lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(seg, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(seg, ui_radius(12), 0);
    lv_obj_set_style_pad_all(seg, UI_S(3), 0);
    ui_flex(seg, LV_FLEX_FLOW_ROW, UI_S(2));
    static const char *const names[] = {"On battery", "Charging"};
    for (int i = 0; i < 2; i++) {
        s_power_btn[i] = ui_btn_text(seg, UI_S(124), UI_S(40), 10, COL_SURFACE2, &font_sora_13m, COL_TEXT2, names[i]);
        lv_obj_add_event_cb(s_power_btn[i], on_power_mode, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

void ui_settings_set_timeouts(const ui_timeouts_t *t)
{
    s_dim_value[0] = t->dim_s;
    s_off_value[0] = t->off_s;
    s_dim_value[1] = t->dim_charging_s;
    s_off_value[1] = t->off_charging_s;
    paint_power_mode();
    choice_select(&s_sleep, t->sleep_h);
}

void ui_settings_board_caps(bool dims, bool battery)
{
    for (size_t i = 0; i < sizeof(s_dimming) / sizeof(s_dimming[0]); i++) {
        lv_obj_set_hidden(s_dimming[i], !dims);
    }
    lv_obj_set_hidden(s_power_row, !battery);
    if (!battery) {
        s_power_mode = 0; /* the controller uses the battery times */
        paint_power_mode();
    }
}

static void build_screen(lv_obj_t *page)
{
    lv_obj_t *head = ui_box(page);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(head, UI_S(8), 0);
    ui_flex(head, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, &font_sora_15m, COL_TEXT, "Brightness");
    s_bright_value = ui_label(head, &font_mono_12, COL_TEXT2, "");
    s_dimming[0] = head;

    s_bright_slider = lv_slider_create(page);
    lv_obj_set_size(s_bright_slider, LV_PCT(100), UI_S(12));
    lv_obj_set_style_margin_ver(s_bright_slider, UI_S(16), 0);
    lv_slider_set_range(s_bright_slider, 1, 100);
    lv_obj_set_style_bg_color(s_bright_slider, COL_SURFACE3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bright_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bright_slider, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bright_slider, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_bright_slider, UI_S(10), LV_PART_KNOB);
    /* Make the whole row easy to grab, not just the 12 px track. */
    lv_obj_set_ext_click_area(s_bright_slider, UI_S(20));
    lv_obj_add_event_cb(s_bright_slider, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    s_dimming[1] = s_bright_slider;

    s_dimming[2] = divider(page);
    lv_obj_t *turn = setting_row(page, "Rotate 180\xC2\xB0");
    s_turn_switch = module_switch(turn);
    lv_obj_add_event_cb(s_turn_switch, on_turn, LV_EVENT_VALUE_CHANGED, NULL);
    divider(page);
    choice_row(page, "Turn off after", &s_sleep);
    divider(page);
    power_row(page);
    s_dimming[3] = choice_row(page, "Dim after", &s_dim);
    choice_row(page, "Screen off after", &s_off);
    paint_power_mode();
}

/* --- Appearance ---------------------------------------------------------------- */

static void theme_mark(void)
{
    for (size_t i = 0; i < ui_theme_count() && i < THEMES_MAX; i++) {
        bool on = i == s_theme_pending;
        lv_obj_set_style_border_color(s_theme_tile[i], on ? COL_ACCENT_TEXT : COL_SURFACE1, 0);
    }
    lv_obj_set_hidden(s_theme_apply, ui_theme_at(s_theme_pending) == ui_theme());
}

static void on_theme_tile(lv_event_t *e)
{
    s_theme_pending = (size_t)(intptr_t)lv_event_get_user_data(e);
    theme_mark();
}

static void on_theme_apply(lv_event_t *e)
{
    ui_action_t a = {.type = UI_ACT_THEME, .text = ui_theme_at(s_theme_pending)->id};
    ui_emit(&a);
}

/* A small block of one color, for the previews. */
static lv_obj_t *swatch(lv_obj_t *parent, lv_color_t color, int32_t w, int32_t h, int32_t radius)
{
    lv_obj_t *s = ui_box(parent);
    lv_obj_set_size(s, w, h);
    lv_obj_set_style_bg_color(s, color, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s, radius, 0);
    return s;
}

/* A thumbnail of the remote in theme th's own colors: a top bar, a panel
 * with text and a filled button, and a column of keys. */
static void theme_preview(lv_obj_t *parent, const ui_theme_t *th)
{
    int32_t r = th->square ? UI_S(2) : UI_S(8);
    lv_obj_t *box = swatch(parent, th->bg, LV_PCT(100), UI_S(124), th->square ? UI_S(3) : UI_S(14));
    lv_obj_set_style_pad_all(box, UI_S(10), 0);
    ui_flex(box, LV_FLEX_FLOW_COLUMN, UI_S(8));

    lv_obj_t *bar = ui_box(box);
    lv_obj_set_size(bar, LV_PCT(100), UI_S(14));
    ui_flex(bar, LV_FLEX_FLOW_ROW, UI_S(6));
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    swatch(bar, th->surface1, UI_S(64), UI_S(14), r);
    swatch(bar, th->accent_text, UI_S(10), UI_S(10), LV_RADIUS_CIRCLE);

    lv_obj_t *body = ui_box(box);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    ui_flex(body, LV_FLEX_FLOW_ROW, UI_S(6));
    swatch(body, th->surface2, UI_S(26), LV_PCT(100), r);
    lv_obj_t *panel = swatch(body, th->surface1, UI_S(10), LV_PCT(100), r);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_pad_all(panel, UI_S(8), 0);
    ui_flex(panel, LV_FLEX_FLOW_COLUMN, UI_S(5));
    swatch(panel, th->text, LV_PCT(70), UI_S(6), UI_S(3));
    swatch(panel, th->text2, LV_PCT(45), UI_S(5), UI_S(3));
    lv_obj_t *spacer = ui_box(panel);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *btns = ui_box(panel);
    lv_obj_set_size(btns, LV_PCT(100), UI_S(16));
    ui_flex(btns, LV_FLEX_FLOW_ROW, UI_S(5));
    swatch(btns, th->accent, UI_S(44), UI_S(16), th->square ? UI_S(2) : UI_S(6));
    swatch(btns, th->ir, UI_S(16), UI_S(16), th->square ? UI_S(2) : UI_S(6));
}

static void build_appearance(lv_obj_t *page)
{
    subhead(page, "Theme");
    lv_obj_t *grid = ui_box(page);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, UI_S(14), 0);
    lv_obj_set_style_pad_column(grid, UI_S(12), 0);
    for (size_t i = 0; i < ui_theme_count() && i < THEMES_MAX; i++) {
        const ui_theme_t *th = ui_theme_at(i);
        if (th == ui_theme()) {
            s_theme_pending = i;
        }
        lv_obj_t *tile = s_theme_tile[i] = ui_box(grid);
        lv_obj_set_size(tile, TILE_W, LV_SIZE_CONTENT);
        lv_obj_set_clickable(tile, true);
        lv_obj_set_style_pad_all(tile, UI_S(4), 0);
        lv_obj_set_style_border_width(tile, UI_S(3), 0);
        lv_obj_set_style_radius(tile, ui_radius(18), 0);
        lv_obj_set_style_opa(tile, LV_OPA_70, LV_STATE_PRESSED);
        ui_flex(tile, LV_FLEX_FLOW_COLUMN, UI_S(8));
        lv_obj_add_event_cb(tile, on_theme_tile, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        theme_preview(tile, th);
        lv_obj_t *name = ui_box(tile);
        lv_obj_set_size(name, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(name, UI_S(4), 0);
        lv_obj_set_style_pad_bottom(name, UI_S(4), 0);
        ui_flex(name, LV_FLEX_FLOW_ROW, UI_S(6));
        lv_obj_set_flex_align(name, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label(name, &font_sora_15m, COL_TEXT, th->name);
        if (th == ui_theme()) {
            ui_label(name, &font_icons_16, COL_ACCENT_TEXT, ICON_CHECK);
        }
    }
    lv_obj_t *spacer = ui_box(page);
    lv_obj_set_flex_grow(spacer, 1);
    s_theme_apply = ui_btn_icon(page, LV_PCT(100), UI_S(52), 14, COL_ACCENT, &font_icons_20, ICON_REFRESH, COL_ON_ACCENT,
                                "Restart to apply", false);
    lv_obj_add_event_cb(s_theme_apply, on_theme_apply, LV_EVENT_CLICKED, NULL);
    theme_mark();
}

/* --- Modules ------------------------------------------------------------------- */

static void on_module_switch(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_current_target(e);
    const hub_module_t *m = lv_event_get_user_data(e);
    ui_action_t a = {.type = UI_ACT_MODULE_ENABLE, .text = m->id, .index = lv_obj_has_state(sw, LV_STATE_CHECKED)};
    ui_emit(&a);
}

static lv_obj_t *module_switch(lv_obj_t *parent)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, UI_S(56), UI_S(30));
    lv_obj_set_style_bg_color(sw, COL_SURFACE3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COL_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -UI_S(3), LV_PART_KNOB);
    lv_obj_set_ext_click_area(sw, UI_S(12));
    return sw;
}

/* --- A module's settings ------------------------------------------------------ */

static void mset_emit(const mod_setting_t *ms, int32_t value, const char *text)
{
    char name[HUB_MODULE_ID_MAX + 18];
    snprintf(name, sizeof(name), "%s/%s", ms->module->id, ms->setting->key);
    ui_action_t a = {.type = UI_ACT_MODULE_SETTING, .name = name, .index = (int)value, .text = text ? text : ""};
    ui_emit(&a);
}

/* Shows ms's value; text for a TEXT setting (NULL: read it). */
static void mset_paint(mod_setting_t *ms, const char *text)
{
    const hub_setting_t *s = ms->setting;
    switch (s->type) {
    case HUB_SETTING_SWITCH:
        lv_obj_set_state(ms->control, LV_STATE_CHECKED, ms->value != 0);
        break;
    case HUB_SETTING_CHOICE:
        if (s->count <= 3) {
            for (size_t i = 0; i < s->count; i++) {
                bool on = (int32_t)i == ms->value;
                lv_obj_set_style_bg_color(ms->seg[i], on ? COL_SURFACE3 : COL_SURFACE1, 0);
                lv_obj_set_style_text_color(lv_obj_get_child(ms->seg[i], 0), on ? COL_ACCENT_TEXT : COL_TEXT2, 0);
            }
        } else {
            lv_label_set_text(ms->control, s->choices[ms->value]);
        }
        break;
    case HUB_SETTING_NUMBER:
        if (s->zero && ms->value == 0) {
            lv_label_set_text(ms->control, s->zero);
        } else {
            lv_label_set_text_fmt(ms->control, "%ld%s%s", (long)ms->value, s->unit ? " " : "", s->unit ? s->unit : "");
        }
        break;
    case HUB_SETTING_TEXT: {
        char value[64];
        if (!text) {
            hub_setting_str(ms->module, s->key, value, sizeof(value));
            text = value;
        }
        lv_label_set_text(ms->control, text[0] ? text : "Not set");
        lv_obj_set_style_text_color(ms->control, text[0] ? COL_TEXT2 : COL_TEXT3, 0);
        break;
    }
    }
}

static void module_settings_refresh(void)
{
    for (size_t i = 0; i < s_mset_count; i++) {
        mod_setting_t *ms = &s_mset[i];
        ms->value = hub_setting_i32(ms->module, ms->setting->key);
        mset_paint(ms, NULL);
    }
}

static void mset_set(mod_setting_t *ms, int32_t value)
{
    ms->value = value;
    mset_paint(ms, NULL);
    mset_emit(ms, value, NULL);
}

static void on_mset_switch(lv_event_t *e)
{
    mod_setting_t *ms = lv_event_get_user_data(e);
    mset_set(ms, lv_obj_has_state(ms->control, LV_STATE_CHECKED));
}

static void on_mset_seg(lv_event_t *e)
{
    mod_setting_t *ms = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    for (size_t i = 0; i < ms->setting->count; i++) {
        if (ms->seg[i] == btn) {
            mset_set(ms, (int32_t)i);
        }
    }
}

static void on_mset_picked(int choice, void *user_data)
{
    if (choice >= 0) {
        mset_set(user_data, choice);
    }
}

static void on_mset_list(lv_event_t *e)
{
    mod_setting_t *ms = lv_event_get_user_data(e);
    ui_modal_list(ms->setting->label, ms->setting->choices, ms->setting->count, ms->value, on_mset_picked, ms);
}

static void on_mset_step(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    mod_setting_t *ms = lv_event_get_user_data(e);
    const hub_setting_t *s = ms->setting;
    int by = (int)(intptr_t)lv_obj_get_user_data(btn); /* -1 or +1 */
    if (s->values && s->count) { /* the next value in the list */
        size_t i = 0;
        while (i + 1 < s->count && s->values[i] < ms->value) {
            i++;
        }
        i = by < 0 ? (i > 0 ? i - 1 : 0) : (s->values[i] > ms->value ? i : (i + 1 < s->count ? i + 1 : i));
        mset_set(ms, s->values[i]);
        return;
    }
    int32_t step = s->step > 0 ? s->step : 1;
    int32_t v = ms->value + by * step;
    mset_set(ms, v < s->min ? s->min : v > s->max ? s->max : v);
}

static void on_mset_text_done(const char *text)
{
    mod_setting_t *ms = s_mset_editing;
    s_mset_editing = NULL;
    ui_show_view(VIEW_SETTINGS);
    if (ms) {
        mset_paint(ms, text);
        mset_emit(ms, 0, text);
    }
}

static void on_mset_text_cancel(void)
{
    s_mset_editing = NULL;
    ui_show_view(VIEW_SETTINGS);
}

static void on_mset_text(lv_event_t *e)
{
    mod_setting_t *ms = lv_event_get_user_data(e);
    char value[64];
    hub_setting_str(ms->module, ms->setting->key, value, sizeof(value));
    s_mset_editing = ms;
    size_t max_len = ms->setting->max_len && ms->setting->max_len < sizeof(value) ? ms->setting->max_len
                                                                                  : sizeof(value) - 1;
    ui_kb_open_local(value, ms->setting->label, max_len, on_mset_text_done, on_mset_text_cancel);
}

/* The value on the right of a row, with a chevron: tapping the row edits it. */
static lv_obj_t *mset_value_btn(lv_obj_t *row, mod_setting_t *ms, lv_event_cb_t cb)
{
    lv_obj_set_clickable(row, true);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, ms);
    lv_obj_t *value = ui_label(row, &font_sora_15m, COL_TEXT2, "");
    lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(value, PANE_W / 2, 0);
    ui_label(row, &font_icons_20, COL_TEXT3, ICON_CHEVRON_RIGHT);
    return value;
}

/* One setting's row: its label, and the control for its type. */
static void mset_row(lv_obj_t *box, mod_setting_t *ms)
{
    const hub_setting_t *s = ms->setting;
    lv_obj_t *row = ui_box(box);
    lv_obj_set_size(row, LV_PCT(100), UI_S(56));
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = ui_label(row, &font_sora_15m, COL_TEXT, s->label);
    lv_obj_set_flex_grow(label, 1);
    switch (s->type) {
    case HUB_SETTING_SWITCH:
        ms->control = module_switch(row);
        lv_obj_add_event_cb(ms->control, on_mset_switch, LV_EVENT_VALUE_CHANGED, ms);
        break;
    case HUB_SETTING_CHOICE:
        if (s->count <= 3) {
            lv_obj_t *seg = ui_box(row);
            lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(seg, COL_SURFACE1, 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(seg, ui_radius(12), 0);
            lv_obj_set_style_pad_all(seg, UI_S(3), 0);
            ui_flex(seg, LV_FLEX_FLOW_ROW, UI_S(2));
            for (size_t i = 0; i < s->count; i++) {
                ms->seg[i] = ui_btn_text(seg, UI_S(110), UI_S(40), 10, COL_SURFACE1, &font_sora_13m, COL_TEXT2,
                                         s->choices[i]);
                lv_obj_add_event_cb(ms->seg[i], on_mset_seg, LV_EVENT_CLICKED, ms);
            }
        } else {
            ms->control = mset_value_btn(row, ms, on_mset_list);
        }
        break;
    case HUB_SETTING_NUMBER: {
        lv_obj_t *minus = ui_btn_icon(row, UI_S(40), UI_S(40), 10, COL_SURFACE1, &font_icons_20, ICON_MINUS, COL_TEXT,
                                      NULL, false);
        ms->control = ui_label(row, &font_sora_15m, COL_TEXT, "");
        lv_obj_set_style_min_width(ms->control, UI_S(64), 0);
        lv_obj_set_style_text_align(ms->control, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *plus = ui_btn_icon(row, UI_S(40), UI_S(40), 10, COL_SURFACE1, &font_icons_20, ICON_PLUS, COL_TEXT,
                                     NULL, false);
        lv_obj_set_user_data(minus, (void *)(intptr_t)-1);
        lv_obj_set_user_data(plus, (void *)(intptr_t)1);
        lv_obj_add_event_cb(minus, on_mset_step, LV_EVENT_CLICKED, ms);
        lv_obj_add_event_cb(plus, on_mset_step, LV_EVENT_CLICKED, ms);
        break;
    }
    case HUB_SETTING_TEXT:
        ms->control = mset_value_btn(row, ms, on_mset_text);
        break;
    }
}

/* Called whenever the modules or their on/off state change. The rows are
 * built once the module list is final (after the modules started). */
void ui_settings_modules_changed(void)
{
    size_t n = hub_module_count();
    bool same = n == s_mod_count;
    for (size_t i = 0; same && i < n; i++) {
        same = s_mod_of[i] == hub_module_get(i);
    }
    if (!same) {
        lv_obj_clean(s_mod_list);
        s_mset_count = 0;
        s_mod_count = n < HUB_MODULES_MAX ? n : HUB_MODULES_MAX;
        for (size_t i = 0; i < s_mod_count; i++) {
            const hub_module_t *m = hub_module_get(i);
            s_mod_of[i] = m;
            lv_obj_t *card = ui_box(s_mod_list);
            lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(card, COL_SURFACE2, 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(card, ui_radius(16), 0);
            lv_obj_set_style_pad_hor(card, UI_S(14), 0);
            ui_flex(card, LV_FLEX_FLOW_COLUMN, 0);
            lv_obj_t *row = ui_box(card);
            lv_obj_set_size(row, LV_PCT(100), UI_S(64));
            ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(12));
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            icon_box(row, m->icon ? m->icon : ICON_GRID, COL_ACCENT_TEXT);
            lv_obj_t *name = ui_label(row, &font_sora_15m, COL_TEXT, m->name);
            lv_obj_set_flex_grow(name, 1);
            s_mod_switch[i] = module_switch(row);
            lv_obj_add_event_cb(s_mod_switch[i], on_module_switch, LV_EVENT_VALUE_CHANGED, (void *)m);

            s_mod_settings[i] = NULL;
            if (m->settings_count == 0) {
                continue;
            }
            lv_obj_t *box = ui_box(card);
            lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
            ui_flex(box, LV_FLEX_FLOW_COLUMN, 0);
            s_mod_settings[i] = box;
            for (size_t k = 0; k < m->settings_count && s_mset_count < MOD_SETTINGS_MAX; k++) {
                mod_setting_t *ms = &s_mset[s_mset_count++];
                *ms = (mod_setting_t){.module = m, .setting = &m->settings[k]};
                divider(box);
                mset_row(box, ms);
            }
        }
        module_settings_refresh();
    }
    for (size_t i = 0; i < s_mod_count; i++) {
        bool on = hub_module_enabled(s_mod_of[i]);
        lv_obj_set_state(s_mod_switch[i], LV_STATE_CHECKED, on);
        if (s_mod_settings[i]) {
            lv_obj_set_hidden(s_mod_settings[i], !on);
        }
    }
    lv_obj_set_hidden(s_mod_none, s_mod_count > 0);
}

/* --- Time zone (at the top of Modules) -------------------------------------------- */

#define TZ_REGIONS_MAX 16
#define TZ_LABEL_MAX   40

static lv_obj_t *s_tz_value;
static char s_tz[48];                            /* the zone in use; "" = Automatic */
static char s_tz_regions[TZ_REGIONS_MAX][16];    /* "Africa" ... "UTC", in the list's order */
static size_t s_tz_region_count;
static char s_tz_region[16];                     /* the region whose zones are listed */
static char (*s_tz_labels)[TZ_LABEL_MAX];        /* its zones' labels ("New York") */
static size_t *s_tz_index;                       /* and their hub_time_zone_name() index */

/* How a region is shown: IANA's names, with "America" covering both. */
static const char *region_label(const char *region)
{
    return strcmp(region, "America") == 0  ? "Americas"
           : strcmp(region, "Indian") == 0 ? "Indian Ocean"
                                           : region;
}

/* "America/Argentina/Buenos_Aires" -> "Argentina / Buenos Aires"; "UTC" stays. */
static void zone_label(const char *name, char *out, size_t len)
{
    const char *city = strchr(name, '/');
    city = city ? city + 1 : name;
    size_t n = 0;
    for (; *city && n + 4 < len; city++) {
        if (*city == '/') {
            n += (size_t)snprintf(out + n, len - n, " / ");
        } else {
            out[n++] = *city == '_' ? ' ' : *city;
        }
    }
    out[n] = '\0';
}

static bool in_region(const char *name, const char *region)
{
    size_t n = strlen(region);
    return strncmp(name, region, n) == 0 && (name[n] == '/' || name[n] == '\0');
}

static void tz_emit(const char *name)
{
    ui_action_t a = {.type = UI_ACT_TIME_ZONE, .text = name};
    ui_emit(&a);
}

static void on_tz_zone(int choice, void *user_data)
{
    if (choice >= 0) {
        tz_emit(hub_time_zone_name(s_tz_index[choice]));
    }
}

static void on_tz_region(int choice, void *user_data)
{
    if (choice == 0) {
        tz_emit(""); /* Automatic */
    } else if (choice > 0) {
        const char *region = s_tz_regions[choice - 1];
        if (strcmp(region, "UTC") == 0) {
            tz_emit("UTC"); /* a zone of its own, no cities */
        } else {
            ui_settings_pick_time_zone(region);
        }
    }
}

void ui_settings_pick_time_zone(const char *region)
{
    size_t total = hub_time_zone_count();
    if (!region) {
        /* Automatic, then each region once (the names are sorted). */
        static const char *items[TZ_REGIONS_MAX + 1];
        items[0] = "Automatic";
        s_tz_region_count = 0;
        int selected = s_tz[0] ? -1 : 0;
        for (size_t i = 0; i < total && s_tz_region_count < TZ_REGIONS_MAX; i++) {
            const char *name = hub_time_zone_name(i);
            size_t n = strcspn(name, "/");
            char *last = s_tz_region_count ? s_tz_regions[s_tz_region_count - 1] : NULL;
            if (last && strlen(last) == n && strncmp(last, name, n) == 0) {
                continue;
            }
            char *r = s_tz_regions[s_tz_region_count];
            snprintf(r, sizeof(s_tz_regions[0]), "%.*s", (int)n, name);
            if (s_tz[0] && in_region(s_tz, r)) {
                selected = (int)s_tz_region_count + 1;
            }
            items[++s_tz_region_count] = region_label(r);
        }
        ui_modal_list("Time zone", items, s_tz_region_count + 1, selected, on_tz_region, NULL);
        return;
    }
    if (!s_tz_labels || !s_tz_index) {
        lv_free(s_tz_labels); /* both or neither: a half-made pair isn't kept */
        lv_free(s_tz_index);
        s_tz_labels = lv_malloc(total * sizeof(*s_tz_labels));
        s_tz_index = lv_malloc(total * sizeof(*s_tz_index));
        if (!s_tz_labels || !s_tz_index) {
            lv_free(s_tz_labels);
            lv_free(s_tz_index);
            s_tz_labels = NULL;
            s_tz_index = NULL;
            return;
        }
    }
    static const char **items;
    if (!items) {
        items = lv_malloc(total * sizeof(*items));
        if (!items) {
            return;
        }
    }
    strlcpy(s_tz_region, region, sizeof(s_tz_region));
    size_t count = 0;
    int selected = -1;
    for (size_t i = 0; i < total; i++) {
        const char *name = hub_time_zone_name(i);
        if (!in_region(name, region)) {
            continue;
        }
        zone_label(name, s_tz_labels[count], TZ_LABEL_MAX);
        s_tz_index[count] = i;
        if (strcmp(name, s_tz) == 0) {
            selected = (int)count;
        }
        items[count] = s_tz_labels[count];
        count++;
    }
    ui_modal_list(region_label(region), items, count, selected, on_tz_zone, NULL);
}

void ui_settings_set_time_zone(const char *name)
{
    strlcpy(s_tz, name, sizeof(s_tz));
    if (s_tz_value) {
        char label[TZ_LABEL_MAX];
        zone_label(name, label, sizeof(label));
        lv_label_set_text(s_tz_value, name[0] ? label : "Automatic");
    }
}

static void on_tz_row(lv_event_t *e)
{
    ui_settings_pick_time_zone(NULL);
}

static void build_time_zone(lv_obj_t *page)
{
    lv_obj_t *row = ui_btn(page, LV_PCT(100), UI_S(64), 16, COL_SURFACE2);
    lv_obj_set_style_pad_hor(row, UI_S(14), 0);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    icon_box(row, ICON_CLOCK, COL_ACCENT_TEXT);
    lv_obj_t *title = ui_label(row, &font_sora_15m, COL_TEXT, "Time zone");
    lv_obj_set_flex_grow(title, 1);
    s_tz_value = ui_label(row, &font_sora_15m, COL_TEXT2, "Automatic");
    ui_label(row, &font_icons_20, COL_TEXT3, ICON_CHEVRON_RIGHT);
    lv_obj_add_event_cb(row, on_tz_row, LV_EVENT_CLICKED, NULL);
    ui_settings_set_time_zone(s_tz);
}

static void on_arrange(lv_event_t *e)
{
    ui_show_view(VIEW_DASH);
    ui_dash_arrange(true);
}

static void build_arrange(lv_obj_t *page)
{
    lv_obj_t *row = ui_btn(page, LV_PCT(100), UI_S(64), 16, COL_SURFACE2);
    lv_obj_set_style_pad_hor(row, UI_S(14), 0);
    ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    icon_box(row, ICON_GRID, COL_ACCENT_TEXT);
    lv_obj_t *title = ui_label(row, &font_sora_15m, COL_TEXT, "Arrange Home");
    lv_obj_set_flex_grow(title, 1);
    ui_label(row, &font_icons_20, COL_TEXT3, ICON_CHEVRON_RIGHT);
    lv_obj_add_event_cb(row, on_arrange, LV_EVENT_CLICKED, NULL);
}

static void build_modules(lv_obj_t *page)
{
    page_scrolls(page); /* the modules' settings outgrow the screen */
    build_time_zone(page);
    build_arrange(page);
    subhead(page, "Modules");
    s_mod_none = ui_label(page, &font_sora_15m, COL_TEXT3, "No modules");
    s_mod_list = ui_box(page);
    lv_obj_set_size(s_mod_list, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(s_mod_list, LV_FLEX_FLOW_COLUMN, UI_S(10));
}

/* --- About --------------------------------------------------------------------- */

void ui_settings_set_hostname(const char *name)
{
    strlcpy(s_hostname, name ? name : "", sizeof(s_hostname));
}

void ui_settings_set_token(const char *token)
{
    strlcpy(s_token, token ? token : "", sizeof(s_token));
}

static void on_token_show(lv_event_t *e)
{
    s_token_shown = !s_token_shown;
    about_refresh();
}

static void about_refresh(void);

void ui_settings_set_about(const ui_about_t *about)
{
    s_about_info = *about;
    if (s_section == SEC_ABOUT && ui_current_view() == VIEW_SETTINGS) {
        about_refresh();
    }
}

void ui_settings_set_build(const char *version, const char *date, const char *time)
{
    strlcpy(s_version, version ? version : "", sizeof(s_version));
    strlcpy(s_built_date, date, sizeof(s_built_date));
    strlcpy(s_built_time, time, sizeof(s_built_time));
    s_version_text[0] = '\0';
}

/* "v0.1.0-dev · Oct 13 23:10" for the top bar while Settings is open: the
 * version (version.txt) and the build time, so every update shows even
 * when the version doesn't change. */
const char *ui_settings_version_text(void)
{
    if (!s_version_text[0]) {
        if (s_built_date[0]) {
            snprintf(s_version_text, sizeof(s_version_text), "v%s \xC2\xB7 %.6s %s", s_version, s_built_date,
                     s_built_time);
        } else {
            snprintf(s_version_text, sizeof(s_version_text), "v%s", s_version);
        }
    }
    return s_version_text;
}

static void on_update_btn(lv_event_t *e)
{
    ui_action_t a = {.type = s_upd == UI_UPDATE_AVAILABLE ? UI_ACT_UPDATE_INSTALL : UI_ACT_UPDATE_CHECK};
    ui_emit(&a);
}

/* The Version row: this version, what the last check found, and Check or
 * Install. */
static void paint_update(void)
{
    if (!s_upd_btn) {
        return;
    }
    const char *mine = s_version;
    switch (s_upd) {
    case UI_UPDATE_CHECKING:
        lv_label_set_text_fmt(s_about[AB_VERSION], "%s \xC2\xB7 checking", mine);
        break;
    case UI_UPDATE_CURRENT:
        lv_label_set_text_fmt(s_about[AB_VERSION], "%s \xC2\xB7 up to date", mine);
        break;
    case UI_UPDATE_AVAILABLE:
        lv_label_set_text_fmt(s_about[AB_VERSION], "%s \xE2\x86\x92 %s", mine, s_upd_version);
        break;
    case UI_UPDATE_INSTALLING:
        lv_label_set_text_fmt(s_about[AB_VERSION], "Installing %s \xC2\xB7 %d%%", s_upd_version, s_upd_percent);
        break;
    case UI_UPDATE_FAILED:
        lv_label_set_text_fmt(s_about[AB_VERSION], "%s \xC2\xB7 update didn't work", mine);
        break;
    default:
        lv_label_set_text(s_about[AB_VERSION], mine);
        break;
    }
    bool busy = s_upd == UI_UPDATE_CHECKING || s_upd == UI_UPDATE_INSTALLING;
    lv_obj_set_hidden(s_upd_btn, busy);
    ui_spinner_run(s_upd_spinner, busy);
    lv_label_set_text(lv_obj_get_child(s_upd_btn, 0), s_upd == UI_UPDATE_AVAILABLE ? "Install" : "Check");
    lv_obj_set_style_bg_color(s_upd_btn, s_upd == UI_UPDATE_AVAILABLE ? COL_ACCENT : COL_SURFACE2, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_upd_btn, 0),
                                s_upd == UI_UPDATE_AVAILABLE ? COL_ON_ACCENT : COL_TEXT2, 0);
}

void ui_settings_set_update(ui_update_t state, const char *version, int percent)
{
    s_upd = state;
    strlcpy(s_upd_version, version ? version : "", sizeof(s_upd_version));
    s_upd_percent = percent;
    paint_update();
}

static void about_refresh(void)
{
    paint_update();
    lv_label_set_text_fmt(s_about[AB_BUILT], "%s, %s", s_built_date, s_built_time);
    const ui_about_t *a = &s_about_info;
    lv_label_set_text(s_about[AB_SLOT], a->slot[0] ? a->slot : "--");
    lv_label_set_text(s_about[AB_ADDRESS], a->address[0] ? a->address : "Offline");
    lv_label_set_text(s_about[AB_NAME], s_hostname[0] ? s_hostname : "--");
    if (a->rssi) {
        lv_label_set_text_fmt(s_about[AB_SIGNAL], "%d dBm", a->rssi);
    } else {
        lv_label_set_text(s_about[AB_SIGNAL], "--");
    }

    unsigned min = (unsigned)a->uptime_min;
    if (min >= 24 * 60) {
        lv_label_set_text_fmt(s_about[AB_UPTIME], "%u d %u h", min / (24 * 60), min / 60 % 24);
    } else if (min >= 60) {
        lv_label_set_text_fmt(s_about[AB_UPTIME], "%u h %u min", min / 60, min % 60);
    } else {
        lv_label_set_text_fmt(s_about[AB_UPTIME], "%u min", min);
    }
    unsigned internal = (unsigned)a->internal_kb;
    unsigned psram = (unsigned)(a->psram_kb * 10 / 1024); /* tenths of a MB */
    lv_label_set_text_fmt(s_about[AB_MEMORY], "%u KB internal \xC2\xB7 %u.%u MB PSRAM", internal, psram / 10,
                          psram % 10);
    if (!s_token[0]) {
        lv_label_set_text(s_about[AB_TOKEN], "--");
    } else if (s_token_shown) {
        lv_label_set_text(s_about[AB_TOKEN], s_token);
    } else {
        lv_label_set_text(s_about[AB_TOKEN], "********");
    }
    lv_obj_set_hidden(s_token_btn, !s_token[0]);
    lv_label_set_text(lv_obj_get_child(s_token_btn, 0), s_token_shown ? "Hide" : "Show");
    /* No token: a release build (no updates from a PC), or before Wi-Fi. */
    for (int i = 0; i < 2; i++) {
        lv_obj_set_hidden(s_token_row[i], !s_token[0]);
    }
}

static void about_tick(lv_timer_t *t)
{
    if (s_section == SEC_ABOUT && ui_current_view() == VIEW_SETTINGS) {
        about_refresh();
    }
}

static void build_about(lv_obj_t *page)
{
    for (int i = 0; i < AB_COUNT; i++) {
        if (i > 0) {
            divider(page);
        }
        if (i == AB_TOKEN) {
            s_token_row[0] = lv_obj_get_child(page, -1);
        }
        lv_obj_t *row = setting_row(page, k_about[i]);
        lv_obj_set_height(row, UI_S(50));
        lv_obj_set_style_text_color(lv_obj_get_child(row, 0), COL_TEXT2, 0);
        if (i == AB_VERSION) {
            /* The version and what the update check found, then Check or Install. */
            lv_obj_t *right = ui_box(row);
            lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            ui_flex(right, LV_FLEX_FLOW_ROW, UI_S(12));
            lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            s_about[i] = ui_label(right, &font_sora_15m, COL_TEXT, "");
            s_upd_spinner = ui_spinner(right, UI_S(22), COL_ACCENT_TEXT);
            ui_spinner_run(s_upd_spinner, false);
            s_upd_btn = ui_btn_text(right, LV_SIZE_CONTENT, UI_S(32), 10, COL_SURFACE2, &font_sora_12, COL_TEXT2, "Check");
            lv_obj_set_style_pad_hor(s_upd_btn, UI_S(12), 0);
            lv_obj_add_event_cb(s_upd_btn, on_update_btn, LV_EVENT_CLICKED, NULL);
            continue;
        }
        if (i == AB_TOKEN) {
            s_token_row[1] = row;
            /* The value, then Show / Hide, on the right. */
            lv_obj_t *right = ui_box(row);
            lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            ui_flex(right, LV_FLEX_FLOW_ROW, UI_S(12));
            lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            s_about[i] = ui_label(right, &font_mono_12, COL_TEXT, "");
            s_token_btn = ui_btn_text(right, LV_SIZE_CONTENT, UI_S(32), 10, COL_SURFACE2, &font_sora_12, COL_TEXT2, "Show");
            lv_obj_set_style_pad_hor(s_token_btn, UI_S(12), 0);
            lv_obj_add_event_cb(s_token_btn, on_token_show, LV_EVENT_CLICKED, NULL);
            continue;
        }
        s_about[i] = ui_label(row, &font_sora_15m, COL_TEXT, "");
    }
    lv_timer_create(about_tick, 2000, NULL);
}

void ui_settings_set_update_badges(bool about, bool remotes)
{
    lv_obj_set_hidden(s_nav_badge[SEC_ABOUT], !about);
    lv_obj_set_hidden(s_nav_badge[SEC_REMOTES], !remotes);
}

/* --- Screen layout ------------------------------------------------------------- */

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *root = ui_box(parent);
    lv_obj_set_size(root, UI_WIDE_W, UI_BODY_H);
    ui_flex(root, LV_FLEX_FLOW_ROW, UI_GAP);

    lv_obj_t *nav = ui_panel(root, NAV_W, UI_BODY_H);
    lv_obj_set_style_pad_all(nav, UI_S(14), 0);
    ui_flex(nav, LV_FLEX_FLOW_COLUMN, UI_S(6));
    for (int i = 0; i < SEC_COUNT; i++) {
        lv_obj_t *b = s_nav[i] = ui_btn(nav, LV_PCT(100), UI_S(52), 14, COL_SURFACE1);
        lv_obj_set_style_pad_hor(b, UI_S(14), 0);
        ui_flex(b, LV_FLEX_FLOW_ROW, UI_S(12));
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label(b, &font_icons_20, COL_TEXT2, k_sections[i].icon);
        ui_label(b, &font_sora_15m, COL_TEXT2, k_sections[i].name);
        lv_obj_add_event_cb(b, on_nav, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_nav_badge[i] = ui_badge(b, 0, 0);
        lv_obj_align(s_nav_badge[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_t *spacer = ui_box(nav);
    lv_obj_set_width(spacer, UI_S(1));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *off = ui_btn_icon(nav, LV_PCT(100), UI_S(52), 14, COL_SURFACE2, &font_icons_20, ICON_POWER, COL_BAD,
                                "Turn off remote", false);
    ui_btn_bind_action(off, UI_ACT_POWER_OFF, NULL, 0);
    lv_obj_t *done = ui_btn_text(nav, LV_PCT(100), UI_S(52), 14, COL_ACCENT, &font_sora_15m, COL_ON_ACCENT, "Done");
    lv_obj_set_style_margin_top(done, UI_S(4), 0);
    lv_obj_add_event_cb(done, on_done, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pane = ui_panel(root, PANE_W, UI_BODY_H);
    s_page[SEC_REMOTES] = page_box(pane, UI_S(12));
    build_remotes(s_page[SEC_REMOTES]);
    s_page[SEC_WIFI] = page_box(pane, UI_S(12));
    build_wifi(s_page[SEC_WIFI]);
    s_page[SEC_SCREEN] = page_box(pane, 0);
    build_screen(s_page[SEC_SCREEN]);
    s_page[SEC_APPEARANCE] = page_box(pane, UI_S(12));
    build_appearance(s_page[SEC_APPEARANCE]);
    s_page[SEC_MODULES] = page_box(pane, UI_S(10));
    build_modules(s_page[SEC_MODULES]);
    s_page[SEC_ABOUT] = page_box(pane, 0);
    build_about(s_page[SEC_ABOUT]);
    select_section(SEC_REMOTES);
    return root;
}
