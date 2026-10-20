/*
 * Guest Wi-Fi: a QR code that phones scan to join a guest network, with its
 * name and password beside it. The network is set on its screen (Set up,
 * Edit) or in Settings > Modules > Guest Wi-Fi; nothing else uses it, and
 * the log never shows it.
 *
 * Threads: everything runs on the LVGL task; setting_changed (controller
 * task) only hands over through hub_ui_async().
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "hub.h"
#include "ui_kit.h"

#define MODULE_ID "guest_wifi"
#define SSID_MAX  32
#define PASS_MAX  63
#define QR_MAX    (24 + 2 * (SSID_MAX + PASS_MAX)) /* every character escaped */

static const char *TAG = "guest_wifi";

static const hub_setting_t k_settings[] = {
    {.key = "ssid", .label = "Network name", .type = HUB_SETTING_TEXT, .max_len = SSID_MAX, .wifi = true,
     .wifi_open = "pass"},
    {.key = "pass", .label = "Password", .type = HUB_SETTING_TEXT, .max_len = PASS_MAX},
};

/* Screen and card (LVGL task) */
static lv_obj_t *s_view_body;
static lv_obj_t *s_view_status;
static lv_obj_t *s_view_qr;
static lv_obj_t *s_view_name;
static lv_obj_t *s_view_pass;
static lv_obj_t *s_view_pass_head;
static lv_obj_t *s_card_body;
static lv_obj_t *s_card_status;
static lv_obj_t *s_card_qr;
static lv_obj_t *s_card_name;
static lv_obj_t *s_card_pass;

static char s_ssid[SSID_MAX + 1];
static char s_pass[PASS_MAX + 1];

static const hub_module_t *self(void)
{
    return hub_module_find(MODULE_ID);
}

static void load(void)
{
    hub_setting_str(self(), "ssid", s_ssid, sizeof(s_ssid));
    hub_setting_str(self(), "pass", s_pass, sizeof(s_pass));
}

/* Wi-Fi's QR format: WIFI:T:WPA;S:name;P:password;; with \ ; , : " escaped,
 * T:nopass for an open network. */
static void escape_into(char *out, size_t len, const char *text)
{
    size_t at = strlen(out);
    for (const char *p = text; *p && at + 2 < len; p++) {
        if (strchr("\\;,:\"", *p)) {
            out[at++] = '\\';
        }
        out[at++] = *p;
    }
    out[at] = '\0';
}

static void qr_text(char *out, size_t len)
{
    snprintf(out, len, "WIFI:T:%s;S:", s_pass[0] ? "WPA" : "nopass");
    escape_into(out, len, s_ssid);
    if (s_pass[0]) {
        strlcat(out, ";P:", len);
        escape_into(out, len, s_pass);
    }
    strlcat(out, ";;", len);
}

/* A QR code is dark on light in every theme: scanners need it that way, so
 * these two colors stay fixed. It sits in a light rounded square. */
static lv_obj_t *qr_box(lv_obj_t *parent, int32_t size)
{
    lv_obj_t *box = ui_box(parent);
    lv_obj_set_size(box, size, size);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, ui_radius(16), 0);
    lv_obj_t *qr = lv_qrcode_create(box);
    lv_qrcode_set_size(qr, size - UI_S(8));
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, true);
    lv_obj_center(qr);
    return qr;
}

static void show(void *arg)
{
    load();
    bool set = s_ssid[0] != '\0';
    char qr[QR_MAX];
    qr_text(qr, sizeof(qr));
    if (s_view_body) {
        lv_obj_set_hidden(s_view_body, !set);
        ui_status_set(s_view_status, set ? UI_STATUS_NONE : UI_STATUS_EMPTY, "No guest network", NULL);
        if (set) {
            lv_qrcode_update(s_view_qr, qr, strlen(qr));
            lv_label_set_text(s_view_name, s_ssid);
            lv_label_set_text(s_view_pass, s_pass[0] ? s_pass : "None");
        }
    }
    if (s_card_body) {
        lv_obj_set_hidden(s_card_body, !set);
        ui_status_set(s_card_status, set ? UI_STATUS_NONE : UI_STATUS_EMPTY, "Guest Wi-Fi", "Not set up");
        if (set) {
            lv_qrcode_update(s_card_qr, qr, strlen(qr));
            if (s_card_name) {
                lv_label_set_text(s_card_name, s_ssid);
            }
            if (s_card_pass) {
                lv_label_set_text(s_card_pass, s_pass);
                lv_obj_set_hidden(s_card_pass, !s_pass[0]);
            }
        }
    }
}

/* --- Setting it up: the network from a list, then its password ------------- */

static void back(void)
{
    ui_module_open(MODULE_ID);
    show(NULL);
}

static void on_pass_done(const char *text)
{
    hub_kv_set_str(MODULE_ID, "pass", text);
    ESP_LOGI(TAG, "network set");
    back();
}

static void on_network(const char *ssid, bool secure)
{
    hub_kv_set_str(MODULE_ID, "ssid", ssid);
    if (!secure) {
        hub_kv_set_str(MODULE_ID, "pass", ""); /* an open network: no password to ask */
        ESP_LOGI(TAG, "network set");
        back();
        return;
    }
    load();
    ui_kb_open_local(s_pass, "Password", PASS_MAX, on_pass_done, back);
}

static void on_edit(lv_event_t *e)
{
    load();
    ui_wifi_pick("Guest network", s_ssid, on_network, back);
}

/* --- Screen and card ----------------------------------------------------------- */

static lv_obj_t *create_view(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, HUB_VIEW_W, HUB_VIEW_H);
    lv_obj_set_style_pad_all(p, UI_S(36), 0);

    s_view_status = ui_status_create(p, LV_PCT(100), LV_PCT(100), false);
    ui_status_set_icon(s_view_status, UI_STATUS_EMPTY, &font_icons_28, ICON_QR, COL_TEXT2);
    ui_status_add_action(s_view_status, UI_STATUS_EMPTY, ICON_PENCIL, "Set up", true, on_edit, NULL);

    s_view_body = ui_box(p);
    lv_obj_set_size(s_view_body, LV_PCT(100), LV_PCT(100));
    ui_flex(s_view_body, LV_FLEX_FLOW_ROW, UI_S(48));
    lv_obj_set_flex_align(s_view_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    s_view_qr = qr_box(s_view_body, HUB_VIEW_H - 2 * UI_S(36));

    lv_obj_t *info = ui_box(s_view_body);
    lv_obj_set_height(info, LV_PCT(100));
    lv_obj_set_flex_grow(info, 1);
    ui_flex(info, LV_FLEX_FLOW_COLUMN, UI_S(8));
    ui_label(info, &font_sora_15m, COL_TEXT2, "Network");
    s_view_name = ui_label(info, &font_sora_26m, COL_TEXT, "");
    lv_label_set_long_mode(s_view_name, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_view_name, LV_PCT(100));
    s_view_pass_head = ui_label(info, &font_sora_15m, COL_TEXT2, "Password");
    lv_obj_set_style_margin_top(s_view_pass_head, UI_S(24), 0);
    s_view_pass = ui_label(info, &font_sora_26m, COL_TEXT, "");
    lv_label_set_long_mode(s_view_pass, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_view_pass, LV_PCT(100));
    lv_obj_t *spacer = ui_box(info);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *edit = ui_btn_icon(info, UI_S(160), UI_S(56), 16, COL_SURFACE2, &font_icons_20, ICON_PENCIL, COL_TEXT,
                                 "Edit", false);
    lv_obj_add_event_cb(edit, on_edit, LV_EVENT_CLICKED, NULL);
    show(NULL);
    return p;
}

/* Small: the code and the name. Medium and large: the code beside the name
 * and password. */
static lv_obj_t *create_card(lv_obj_t *parent, hub_card_size_t size)
{
    lv_obj_t *card = ui_panel(parent, HUB_CARD_W(size), HUB_CARD_H);
    lv_obj_set_style_pad_all(card, UI_S(16), 0);
    s_card_status = ui_status_create(card, LV_PCT(100), LV_PCT(100), true);
    ui_status_set_icon(s_card_status, UI_STATUS_EMPTY, &font_icons_24, ICON_QR, COL_TEXT2);

    s_card_body = ui_box(card);
    lv_obj_set_size(s_card_body, LV_PCT(100), LV_PCT(100));
    int32_t inner_h = HUB_CARD_H - 2 * UI_S(16);
    s_card_name = s_card_pass = NULL;
    if (size == HUB_CARD_SMALL) {
        ui_flex(s_card_body, LV_FLEX_FLOW_COLUMN, UI_S(6));
        lv_obj_set_flex_align(s_card_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_card_qr = qr_box(s_card_body, inner_h - UI_S(26));
        s_card_name = ui_label(s_card_body, &font_sora_13m, COL_TEXT2, "");
        lv_label_set_long_mode(s_card_name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_max_width(s_card_name, HUB_CARD_W(size) - 2 * UI_S(16), 0);
    } else {
        ui_flex(s_card_body, LV_FLEX_FLOW_ROW, UI_S(20));
        s_card_qr = qr_box(s_card_body, inner_h);
        lv_obj_t *info = ui_box(s_card_body);
        lv_obj_set_height(info, LV_PCT(100));
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_style_pad_ver(info, UI_S(6), 0);
        ui_flex(info, LV_FLEX_FLOW_COLUMN, UI_S(6));
        lv_obj_t *head = ui_box(info);
        lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
        ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(10));
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label(head, &font_icons_24, COL_ACCENT_TEXT, ICON_QR);
        ui_label(head, &font_sora_16sb, COL_TEXT, "Guest Wi-Fi");
        lv_obj_t *spacer = ui_box(info);
        lv_obj_set_flex_grow(spacer, 1);
        s_card_name = ui_label(info, &font_sora_22m, COL_TEXT, "");
        lv_label_set_long_mode(s_card_name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(s_card_name, LV_PCT(100));
        s_card_pass = ui_label(info, &font_sora_15m, COL_TEXT2, "");
        lv_label_set_long_mode(s_card_pass, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(s_card_pass, LV_PCT(100));
    }
    show(NULL);
    return card;
}

static void on_show(void)
{
    show(NULL);
}

/* --- Module (controller task) ----------------------------------------------------- */

static void event(hub_event_t ev)
{
    switch (ev) {
    case HUB_EV_NETWORK_UP:
    case HUB_EV_NETWORK_DOWN:
    case HUB_EV_SCREEN_ON:
    case HUB_EV_SCREEN_OFF:
    case HUB_EV_MINUTE:
    case HUB_EV_NOW_PLAYING:
    case HUB_EV_TV:
    case HUB_EV_OFF:
    case HUB_EV_CLOCK:
        break;
    }
}

static void setting_changed(const char *key)
{
    hub_ui_async(show, NULL);
}

static const hub_module_t s_module = {
    .id = MODULE_ID,
    .name = "Guest Wi-Fi",
    .icon = ICON_QR,
    .event = event,
    .create_view = create_view,
    .show = on_show,
    .create_card = create_card,
    .card_sizes = HUB_CARD_SMALL | HUB_CARD_MEDIUM | HUB_CARD_LARGE,
    .settings = k_settings,
    .settings_count = sizeof(k_settings) / sizeof(k_settings[0]),
    .setting_changed = setting_changed,
};
HUB_MODULE(s_module);
