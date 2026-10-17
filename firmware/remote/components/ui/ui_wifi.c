/*
 * Wi-Fi network pickers: a scan list (strongest first) with a lock on
 * networks that need a password, and Other network for hidden ones.
 * Choosing one asks for the password on the remote's keyboard, then sends
 * UI_ACT_WIFI_JOIN. Setup (first run) and Settings > Wi-Fi each have one.
 */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"


typedef struct {
    lv_obj_t *msg;
    lv_obj_t *list;
    lv_obj_t *status;
    ui_view_t origin;
} picker_t;

static picker_t s_pickers[UI_WIFI_PICKERS];
static ui_wifi_ap_t s_aps[UI_WIFI_APS_MAX];
static size_t s_ap_count;
static char s_current[33];

static char s_join_ssid[33];
static bool s_join_secure; /* chosen from the list with a lock: needs a password */
static ui_view_t s_join_origin;

/* --- Joining ------------------------------------------------------------------ */

static void join(const char *password)
{
    ui_action_t a = {.type = UI_ACT_WIFI_JOIN, .name = s_join_ssid, .text = password ? password : ""};
    ui_show_view(s_join_origin);
    ui_emit(&a);
}

static void on_password(const char *text)
{
    if (s_join_secure && (!text || !text[0])) {
        ui_show_view(s_join_origin); /* no password for a secured network: nothing to try */
        return;
    }
    join(text);
}

static void on_kb_cancel(void)
{
    ui_show_view(s_join_origin);
}

static void ask_password(void)
{
    char title[64];
    snprintf(title, sizeof(title), "Password for %s", s_join_ssid);
    ui_kb_open_local("", title, 64, on_password, on_kb_cancel);
}

static void on_ap(lv_event_t *e)
{
    const ui_wifi_ap_t *ap = lv_event_get_user_data(e);
    const picker_t *p = lv_obj_get_user_data(lv_event_get_current_target(e));
    strlcpy(s_join_ssid, ap->ssid, sizeof(s_join_ssid));
    s_join_secure = ap->secure;
    s_join_origin = p->origin;
    if (ap->secure) {
        ask_password();
    } else {
        join("");
    }
}

static void on_other_name(const char *text)
{
    if (!text || !text[0]) {
        ui_show_view(s_join_origin);
        return;
    }
    strlcpy(s_join_ssid, text, sizeof(s_join_ssid));
    ask_password(); /* leave it empty for an open network */
}

static void on_other(lv_event_t *e)
{
    const picker_t *p = lv_event_get_user_data(e);
    s_join_origin = p->origin;
    s_join_secure = false; /* a hidden network may be open: an empty password is allowed */
    ui_kb_open_local("", "Network name", 32, on_other_name, on_kb_cancel);
}

static void on_scan(lv_event_t *e)
{
    ui_wifi_scan();
}

/* --- Lists -------------------------------------------------------------------- */

static void fill(picker_t *p)
{
    lv_obj_clean(p->list);
    for (size_t i = 0; i < s_ap_count; i++) {
        const ui_wifi_ap_t *ap = &s_aps[i];
        lv_obj_t *row = ui_btn(p->list, LV_PCT(100), UI_S(56), 14, COL_SURFACE2);
        lv_obj_set_style_pad_hor(row, UI_S(16), 0);
        ui_flex(row, LV_FLEX_FLOW_ROW, UI_S(12));
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_user_data(row, p);
        lv_obj_add_event_cb(row, on_ap, LV_EVENT_CLICKED, (void *)ap);
        /* Signal: strong, fair, weak. */
        lv_color_t c = ap->rssi >= -60 ? COL_TEXT : ap->rssi >= -72 ? COL_TEXT2 : COL_TEXT3;
        ui_label(row, &font_icons_20, c, ICON_WIFI);
        lv_obj_t *name = ui_label(row, &font_sora_15m, COL_TEXT, ap->ssid);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_flex_grow(name, 1);
        if (strcmp(ap->ssid, s_current) == 0) {
            ui_label(row, &font_icons_20, COL_ACCENT_TEXT, ICON_CHECK);
        }
        if (ap->secure) {
            ui_label(row, &font_icons_16, COL_TEXT3, ICON_LOCK);
        }
    }
    ui_status_set(p->status, s_ap_count ? UI_STATUS_NONE : UI_STATUS_EMPTY, s_ap_count ? NULL : "No networks found",
                  NULL);
    lv_obj_set_hidden(p->list, s_ap_count == 0);
}

void ui_wifi_scan(void)
{
    for (int i = 0; i < UI_WIFI_PICKERS; i++) {
        if (s_pickers[i].status) {
            ui_status_set(s_pickers[i].status, UI_STATUS_LOADING, NULL, NULL);
            lv_obj_set_hidden(s_pickers[i].list, true);
        }
    }
    ui_action_t a = {.type = UI_ACT_WIFI_SCAN};
    ui_emit(&a);
}

void ui_wifi_show_networks(const ui_wifi_ap_t *aps, size_t count)
{
    s_ap_count = count < UI_WIFI_APS_MAX ? count : UI_WIFI_APS_MAX;
    for (size_t i = 0; i < s_ap_count; i++) {
        s_aps[i] = aps[i];
    }
    for (int i = 0; i < UI_WIFI_PICKERS; i++) {
        if (s_pickers[i].list) {
            fill(&s_pickers[i]);
        }
    }
}

void ui_wifi_set_current(const char *ssid)
{
    strlcpy(s_current, ssid ? ssid : "", sizeof(s_current));
}

const char *ui_wifi_current(void)
{
    return s_current;
}

void ui_wifi_picker_message(ui_wifi_picker_t which, const char *message)
{
    picker_t *p = &s_pickers[which];
    lv_label_set_text(p->msg, message ? message : "");
    lv_obj_set_hidden(p->msg, !message || !message[0]);
}

lv_obj_t *ui_wifi_picker_create(lv_obj_t *parent, ui_wifi_picker_t which, ui_view_t origin)
{
    picker_t *p = &s_pickers[which];
    p->origin = origin;
    lv_obj_t *root = ui_box(parent);
    lv_obj_set_width(root, LV_PCT(100));
    lv_obj_set_flex_grow(root, 1);
    ui_flex(root, LV_FLEX_FLOW_COLUMN, UI_S(10));

    lv_obj_t *head = ui_box(root);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, &font_sora_13m, COL_TEXT2, "Networks");
    lv_obj_t *scan = ui_btn_icon(head, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE2, &font_icons_16, ICON_REFRESH, COL_TEXT2,
                                 "Scan", false);
    lv_obj_set_style_pad_hor(scan, UI_S(14), 0);
    lv_obj_add_event_cb(scan, on_scan, LV_EVENT_CLICKED, NULL);

    p->msg = ui_label(root, &font_sora_13m, COL_ACCENT_TEXT, "");
    lv_obj_set_width(p->msg, LV_PCT(100));
    lv_label_set_long_mode(p->msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_hidden(p->msg, true);

    p->list = ui_box(root);
    lv_obj_set_width(p->list, LV_PCT(100));
    lv_obj_set_flex_grow(p->list, 1);
    ui_flex(p->list, LV_FLEX_FLOW_COLUMN, UI_S(8));
    lv_obj_set_scrollable(p->list, true);
    lv_obj_set_scroll_dir(p->list, LV_DIR_VER);
    p->status = ui_status_create(root, LV_PCT(100), LV_SIZE_CONTENT, false);
    lv_obj_set_flex_grow(p->status, 1);
    ui_status_set_icon(p->status, UI_STATUS_EMPTY, &font_icons_24, ICON_WIFI_OFF, COL_TEXT3);

    lv_obj_t *other = ui_btn_icon(root, LV_PCT(100), UI_S(52), 14, COL_SURFACE1, &font_icons_20, ICON_ADD, COL_TEXT2,
                                  "Other network", false);
    lv_obj_set_style_border_width(other, UI_S(1), 0);
    lv_obj_set_style_border_color(other, COL_LINE2, 0);
    lv_obj_add_event_cb(other, on_other, LV_EVENT_CLICKED, p);
    fill(p);
    return root;
}
