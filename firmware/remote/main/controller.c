/*
 * The controller task: every event (UI actions, the TV, Wi-Fi, the
 * companion, release checks) is queued here and handled on this one task,
 * which keeps the UI, the TV client and the settings in step. Its parts
 * are in ctrl_*.c (ctrl_internal.h); this file has the queue, the
 * Streamer's keys and the IR actions, what happens when Wi-Fi comes up,
 * and the task itself.
 */
#include "controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "atv_remote.h"
#include "atv_selftest.h"
#include "build_info.h"
#include "companion.h"
#include "ctrl_internal.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hub.h"
#include "ir_ctl.h"
#include "ir_store.h"
#include "net.h"
#include "netlog.h"
#include "nvs.h"
#include "ota.h"
#include "sdkconfig.h"
#include "storage.h"
#include "tv_ir.h"
#include "ui.h"
#include "update.h"

static const char *TAG = "ctrl";

/* Limits written on both sides of the UI's API. */
_Static_assert(COMPANION_MAX_APPS <= UI_MAX_APPS, "the UI must hold every companion app");
_Static_assert(COMPANION_PKG_MAX <= UI_APP_PKG_MAX && COMPANION_LABEL_MAX <= UI_APP_NAME_MAX, "app names differ");
_Static_assert(TV_IR_KEYS_MAX <= UI_TV_KEYS_MAX, "the UI must hold every TV key");
_Static_assert(IR_STORE_LABEL_MAX <= UI_INPUT_NAME_MAX && IR_STORE_LABEL_MAX <= UI_IR_LABEL_MAX, "labels differ");
_Static_assert(IR_STORE_NAME_MAX <= UI_IR_NAME_MAX && IR_STORE_MAX_REMOTES <= UI_IR_MAX_REMOTES, "remotes differ");
_Static_assert(NET_SCAN_MAX <= UI_WIFI_APS_MAX, "the UI must hold every scanned network");

#define TICK_MS         200
#define NVS_HIDDEN_APPS "hidden_apps"
#define NVS_APP_ORDER   "app_order"

typedef enum { MSG_UI, MSG_ATV, MSG_NET, MSG_COMPANION, MSG_UPDATE } msg_kind_t;

typedef struct {
    msg_kind_t kind;
    union {
        ctrl_ui_t ui;
        atv_event_t atv;
        companion_event_t companion;
        update_status_t update;
        struct {
            bool up;
            int reason;
        } net;
    };
} msg_t;

static QueueHandle_t s_q;

/* --- Callbacks: copy and queue, never block -------------------------------- */

static void on_ui(const ui_action_t *a, void *ctx)
{
    msg_t m = {.kind = MSG_UI};
    m.ui.type = a->type;
    m.ui.target = a->target;
    strlcpy(m.ui.name, a->name ? a->name : "", sizeof(m.ui.name));
    m.ui.key = a->key;
    m.ui.dir = a->dir;
    m.ui.index = a->index;
    strlcpy(m.ui.text, a->text ? a->text : "", sizeof(m.ui.text));
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped ui action %d", a->type);
    }
}

static void on_companion(const companion_event_t *evt, void *ctx)
{
    msg_t m = {.kind = MSG_COMPANION, .companion = *evt};
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped companion event %d", evt->type);
    }
}

static void on_update(const update_status_t *status)
{
    msg_t m = {.kind = MSG_UPDATE, .update = *status};
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped update status %d", status->state);
    }
}

static void on_atv(const atv_event_t *evt, void *ctx)
{
    msg_t m = {.kind = MSG_ATV, .atv = *evt};
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped atv event %d", evt->type);
    }
}

static void on_net(bool up, int reason, void *ctx)
{
    msg_t m = {.kind = MSG_NET, .net = {.up = up, .reason = reason}};
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped network %s", up ? "up" : "down");
    }
}

/* --- Helpers --------------------------------------------------------------- */

void controller_log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: internal %u KB free (largest %u KB, lowest %u KB), DMA %u KB (largest %u KB), PSRAM %u KB",
             when, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

/* Home's app order and hidden apps, saved as ui_app_layout() gives them. */
static void load_app_layout(void)
{
    char *order = heap_caps_calloc(2, UI_APP_LIST_MAX, MALLOC_CAP_SPIRAM);
    if (!order) {
        return;
    }
    char *hidden = order + UI_APP_LIST_MAX;
    nvs_handle_t h;
    if (nvs_open(CTRL_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = UI_APP_LIST_MAX;
        if (nvs_get_str(h, NVS_APP_ORDER, order, &len) != ESP_OK) {
            order[0] = '\0';
        }
        len = UI_APP_LIST_MAX;
        if (nvs_get_str(h, NVS_HIDDEN_APPS, hidden, &len) != ESP_OK) {
            hidden[0] = '\0';
        }
        nvs_close(h);
        ui_set_app_layout(order, hidden);
    }
    free(order);
}

static void save_app_layout(void)
{
    char *order = heap_caps_calloc(2, UI_APP_LIST_MAX, MALLOC_CAP_SPIRAM);
    if (!order) {
        return;
    }
    char *hidden = order + UI_APP_LIST_MAX;
    ui_app_layout(order, hidden);
    nvs_handle_t h;
    if (nvs_open(CTRL_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_str(h, NVS_APP_ORDER, order) == ESP_OK && nvs_set_str(h, NVS_HIDDEN_APPS, hidden) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
    free(order);
}

/* --- Message handlers ------------------------------------------------------ */

static void handle_atv(const atv_event_t *e)
{
    switch (e->type) {
    case ATV_EVT_STATE:
    case ATV_EVT_PAIR_RESULT:
    case ATV_EVT_DEVICE_INFO:
        ctrl_setup_atv(e);
        break;
    case ATV_EVT_POWER:
        ui_set_tv_power(e->is_on);
        break;
    case ATV_EVT_APP:
        if (ui_app_is_system_screen(e->app_package)) {
            /* The screensaver, the Assistant...: the app underneath stays. */
            ESP_LOGI(TAG, "app: %s (system screen, ignored)", e->app_package);
            break;
        }
        ESP_LOGI(TAG, "app: %s", e->app_package);
        ui_set_app(e->app_package);
        break;
    case ATV_EVT_VOLUME:
        ESP_LOGI(TAG, "tv volume %u/%u%s", e->volume.level, e->volume.max, e->volume.muted ? " muted" : "");
        ui_set_volume(e->volume.level, e->volume.max, e->volume.muted);
        break;
    case ATV_EVT_IME_SHOW:
        ui_ime_show(e->ime.value, e->ime.label);
        break;
    case ATV_EVT_IME_TEXT:
        ui_ime_text(e->ime.value);
        break;
    }
}

static void handle_ui(const ctrl_ui_t *a)
{
    switch (a->type) {
    case UI_ACT_KEY: {
        bool volume = a->key == UI_KEY_VOLUME_UP || a->key == UI_KEY_VOLUME_DOWN || a->key == UI_KEY_VOLUME_MUTE;
        static const char *const dirs[] = {"", "down", "up", "tap"};
        if (a->target == UI_TARGET_IR_TV) {
            ir_ctl_key(a->key, a->dir);
        } else {
            esp_err_t err = atv_send_key((atv_keycode_t)a->key, (atv_direction_t)a->dir);
            /* One line per key: shows what reached the TV when a key "does nothing". */
            ESP_LOGI(TAG, "key %d %s%s: %s", a->key, a->dir <= UI_DIR_SHORT ? dirs[a->dir] : "?",
                     volume ? " (volume)" : "", esp_err_to_name(err));
        }
        return;
    }
    case UI_ACT_IR:
        ir_ctl_button(a->target, a->text, a->index);
        return;
    case UI_ACT_OPEN_REMOTE:
        ir_ctl_open_remote(a->index);
        return;
    case UI_ACT_REMOTE_RENAME:
        ir_ctl_remote_rename(a->text);
        return;
    case UI_ACT_REMOTE_DELETE:
        ir_ctl_remote_delete();
        return;
    case UI_ACT_BUTTON_RENAME:
        ir_ctl_button_rename(a->index, a->text);
        return;
    case UI_ACT_BUTTON_DELETE:
        ir_ctl_button_delete(a->index);
        return;
    case UI_ACT_TV_BRAND:
        ir_ctl_tv_brand(a->text);
        return;
    case UI_ACT_TV_TRY:
        ir_ctl_tv_try(a->index);
        return;
    case UI_ACT_TV_CHOOSE:
        ir_ctl_tv_choose(a->index);
        return;
    case UI_ACT_TV_LEARNED:
        ir_ctl_tv_learned();
        return;
    case UI_ACT_TV_REMOVE:
        ir_ctl_tv_remove();
        return;
    case UI_ACT_TV_VOLUME:
        ir_ctl_tv_volume(a->index != 0);
        return;
    case UI_ACT_TV_LEARN_KEY:
        ir_ctl_learn_tv_key(a->text);
        return;
    case UI_ACT_LEARN_START:
        ir_ctl_learn_start(a->index, a->name, a->text);
        return;
    case UI_ACT_LEARN_CANCEL:
        ir_ctl_learn_cancel();
        return;
    case UI_ACT_LEARN_TEST:
        ir_ctl_learn_test();
        return;
    case UI_ACT_LEARN_SAVE:
        ir_ctl_learn_save();
        return;
    case UI_ACT_INPUT:
        ir_ctl_input(a->index);
        return;
    case UI_ACT_INPUT_RENAME:
        ir_ctl_input_rename(a->index, a->text);
        return;
    case UI_ACT_INPUT_LEARN:
        ir_ctl_learn_input(a->index);
        return;
    case UI_ACT_APP_LAYOUT:
        save_app_layout();
        return;
    case UI_ACT_LAUNCH:
        atv_launch_app_link(a->text);
        return;
    case UI_ACT_UPDATE_CHECK:
        update_check();
        return;
    case UI_ACT_UPDATE_INSTALL:
        update_install();
        return;
    case UI_ACT_MODULE_ENABLE:
        hub_module_set_enabled(a->text, a->index != 0);
        return;
    default:
        break;
    }
    if (!ctrl_setup_ui(a) && !ctrl_companion_ui(a) && !ctrl_screen_ui(a)) {
        ESP_LOGW(TAG, "ui action %d not handled", a->type);
    }
}

static void handle_net(bool up, int reason)
{
    if (up) {
        netlog_serve();
        net_mdns_start();
        ota_start();
        ui_set_update_token(ota_token());
        ota_mark_good(); /* a fresh update reached Wi-Fi: keep it */
        update_net_up();
        hub_time_start();
    }
    hub_modules_event(up ? HUB_EV_NETWORK_UP : HUB_EV_NETWORK_DOWN);
    ui_set_wifi(up);
    ctrl_setup_net(up, reason);
}

static void handle_update(const update_status_t *u)
{
    static const ui_update_t map[] = {
        [UPDATE_UNKNOWN] = UI_UPDATE_UNKNOWN,       [UPDATE_CHECKING] = UI_UPDATE_CHECKING,
        [UPDATE_CURRENT] = UI_UPDATE_CURRENT,       [UPDATE_AVAILABLE] = UI_UPDATE_AVAILABLE,
        [UPDATE_INSTALLING] = UI_UPDATE_INSTALLING, [UPDATE_FAILED] = UI_UPDATE_FAILED,
    };
    ui_set_update(map[u->state], u->version, u->percent);
    if (u->state == UPDATE_CURRENT || u->state == UPDATE_AVAILABLE) {
        ctrl_companion_release(&u->companion); /* a check finished */
    }
}

/* --- Every tick ---------------------------------------------------------------------------- */

/* mDNS renames the remote if another device has its name ("hubmote-2"):
 * Settings > About shows the name in use. */
static void show_hostname(void)
{
    static char s_name[64];
    char name[64];
    net_hostname(name, sizeof(name));
    if (name[0] && strcmp(name, s_name) != 0) {
        strlcpy(s_name, name, sizeof(s_name));
        char full[72];
        snprintf(full, sizeof(full), "%s.local", name);
        ui_set_hostname(full);
        if (strcmp(name, NET_HOSTNAME) != 0) {
            ESP_LOGW(TAG, "another device is %s.local: this remote is %s", NET_HOSTNAME, full);
        }
    }
}

/* Settings > About: the slot, address, signal, uptime and free memory. */
static void show_about(void)
{
    ui_about_t a = {0};
    const esp_partition_t *part = esp_ota_get_running_partition();
    if (part) {
        strlcpy(a.slot, part->label, sizeof(a.slot));
    }
    esp_netif_ip_info_t ip = {0};
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr) {
        snprintf(a.address, sizeof(a.address), IPSTR, IP2STR(&ip.ip));
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        a.rssi = ap.rssi;
    }
    a.uptime_min = (uint32_t)(esp_timer_get_time() / 60000000LL);
    a.internal_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    a.psram_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    ui_set_about(&a);
}

static void tick(void)
{
    int64_t now = esp_timer_get_time();
    ctrl_screen_tick(now);
    ctrl_companion_tick(now);
    static int s_name_ticks;
    if (++s_name_ticks >= 10) { /* every 2 s */
        s_name_ticks = 0;
        show_hostname();
        show_about();
    }
    static int64_t s_last_minute_us;
    if (now - s_last_minute_us >= 60 * 1000000LL) {
        s_last_minute_us = now;
        hub_modules_event(HUB_EV_MINUTE);
    }
}

static void controller_task(void *arg)
{
#if CONFIG_HM_ATV_SELFTEST
    /* Encoder vs golden vectors from the official protobuf runtime: cheap,
     * no network, and the first proof on hardware that messages are right. */
    esp_err_t st = atv_selftest();
    if (st == ESP_OK) {
        ESP_LOGI(TAG, "atv self-test passed");
    } else {
        ESP_LOGE(TAG, "atv self-test FAILED: %s", esp_err_to_name(st));
    }
#endif

    ctrl_setup_load();
    load_app_layout();
    companion_set_icon_sizes(UI_APP_ICON, UI_APP_ICON_SMALL); /* scaled with the screen */
    ctrl_companion_start(on_companion);
    update_start(on_update);
    controller_log_heap("before IR");
    storage_mount();
    ir_ctl_init();
    companion_set_cache_dir(STORAGE_MOUNT "/icons", 640 * 1024);
    controller_log_heap("after IR");
    hub_modules_init();
    controller_log_heap("after modules");

    ctrl_setup_start(on_net);

    msg_t m;
    for (;;) {
        if (xQueueReceive(s_q, &m, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            switch (m.kind) {
            case MSG_UI:
                handle_ui(&m.ui);
                break;
            case MSG_ATV:
                handle_atv(&m.atv);
                break;
            case MSG_COMPANION:
                ctrl_companion_event(&m.companion);
                break;
            case MSG_NET:
                handle_net(m.net.up, m.net.reason);
                break;
            case MSG_UPDATE:
                handle_update(&m.update);
                break;
            }
        }
        tick();
    }
}

void controller_start(void)
{
    s_q = xQueueCreateWithCaps(32, sizeof(msg_t), MALLOC_CAP_SPIRAM);
    ctrl_screen_load(); /* and the theme, which ui_init() builds with */
    ui_init(on_ui, NULL);
    ctrl_screen_show();
    ui_set_hostname(NET_HOSTNAME ".local");
    ui_set_build(esp_app_get_description()->version, build_date(), build_time());

    atv_config_t cfg = {
        .client_name = CONFIG_HM_CLIENT_NAME,
        .nvs_namespace = "atv",
        .enable_ime = true,
        .on_event = on_atv,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(atv_init(&cfg));

    /* Big stack: RSA-2048 key generation runs here on first boot. */
    xTaskCreatePinnedToCore(controller_task, "ctrl", 16384, NULL, 5, NULL, 0);
}
