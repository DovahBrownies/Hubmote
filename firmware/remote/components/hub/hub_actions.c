/* Modules' actions on the TV, and the companion's data for them (hub_actions.h). */
#include "hub_actions.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hub_module.h"

#define TV_APPS_MAX 48

static hub_action_handler_t s_handler;
static SemaphoreHandle_t s_lock; /* now playing and the app list */
static hub_now_playing_t s_now;
static bool s_has_now;
static hub_tv_app_t *s_apps; /* PSRAM */
static size_t s_app_count;
static portMUX_TYPE s_awake_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_awake;
static hub_tv_state_t s_tv; /* under s_lock */

void hub_set_action_handler(hub_action_handler_t handler)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    s_handler = handler;
}

static esp_err_t queue(const hub_action_t *a)
{
    if (!s_handler) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_handler(a) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t hub_tv_key(hub_key_t key)
{
    hub_action_t a = {.type = HUB_ACTION_KEY, .key = key};
    return queue(&a);
}

esp_err_t hub_ir_tv_key(const char *key)
{
    if (!key || !key[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    hub_action_t a = {.type = HUB_ACTION_IR_TV_KEY};
    strlcpy(a.text, key, sizeof(a.text));
    return queue(&a);
}

esp_err_t hub_tv_off(bool ir)
{
    hub_action_t a = {.type = HUB_ACTION_TV_OFF, .ir = ir};
    return queue(&a);
}

esp_err_t hub_tv_open(const char *app)
{
    if (!app || !app[0] || strlen(app) >= sizeof(((hub_action_t *)0)->text)) {
        return ESP_ERR_INVALID_ARG;
    }
    hub_action_t a = {.type = HUB_ACTION_OPEN};
    strlcpy(a.text, app, sizeof(a.text));
    return queue(&a);
}

esp_err_t hub_notify(const hub_module_t *module, const char *icon, const char *text)
{
    if (!text || !text[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    hub_action_t a = {.type = HUB_ACTION_NOTIFY, .remote = true};
    a.tv = hub_setting_find(module, "tv_msg") && hub_setting_i32(module, "tv_msg") != 0;
    strlcpy(a.text, text, sizeof(a.text));
    strlcpy(a.icon, icon ? icon : "", sizeof(a.icon));
    return queue(&a);
}

esp_err_t hub_tv_notify(const char *text)
{
    if (!text || !text[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    hub_action_t a = {.type = HUB_ACTION_NOTIFY, .tv = true};
    strlcpy(a.text, text, sizeof(a.text));
    return queue(&a);
}

void hub_tv_state(hub_tv_state_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_tv;
    xSemaphoreGive(s_lock);
}

void hub_set_tv_power(bool on)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = s_tv.on != on;
    s_tv.on = on;
    xSemaphoreGive(s_lock);
    if (changed) {
        hub_modules_event(HUB_EV_TV);
    }
}

void hub_set_tv_app(const char *app)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = strcmp(s_tv.app, app ? app : "") != 0;
    strlcpy(s_tv.app, app ? app : "", sizeof(s_tv.app));
    xSemaphoreGive(s_lock);
    if (changed) {
        hub_modules_event(HUB_EV_TV);
    }
}

bool hub_now_playing(hub_now_playing_t *out)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool has = s_has_now;
    if (has) {
        *out = s_now;
    }
    xSemaphoreGive(s_lock);
    return has;
}

void hub_set_now_playing(const hub_now_playing_t *now)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = (now != NULL) != s_has_now || (now && memcmp(now, &s_now, sizeof(s_now)) != 0);
    s_has_now = now != NULL;
    if (now) {
        s_now = *now;
    }
    xSemaphoreGive(s_lock);
    if (changed) {
        hub_modules_event(HUB_EV_NOW_PLAYING);
    }
}

size_t hub_tv_apps(hub_tv_app_t *out, size_t max)
{
    if (!s_lock) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_app_count < max ? s_app_count : max;
    if (n) {
        memcpy(out, s_apps, n * sizeof(*out));
    }
    xSemaphoreGive(s_lock);
    return n;
}

void hub_set_tv_apps(const hub_tv_app_t *apps, size_t count)
{
    if (!s_lock) {
        return;
    }
    if (!s_apps && count) {
        s_apps = heap_caps_calloc(TV_APPS_MAX, sizeof(*s_apps), MALLOC_CAP_SPIRAM);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_app_count = s_apps ? (count < TV_APPS_MAX ? count : TV_APPS_MAX) : 0;
    if (s_app_count) {
        memcpy(s_apps, apps, s_app_count * sizeof(*s_apps));
    }
    xSemaphoreGive(s_lock);
}

void hub_hold_awake(void)
{
    taskENTER_CRITICAL(&s_awake_mux);
    s_awake++;
    taskEXIT_CRITICAL(&s_awake_mux);
}

void hub_release_awake(void)
{
    taskENTER_CRITICAL(&s_awake_mux);
    if (s_awake > 0) {
        s_awake--;
    }
    taskEXIT_CRITICAL(&s_awake_mux);
}

bool hub_awake_held(void)
{
    taskENTER_CRITICAL(&s_awake_mux);
    bool held = s_awake > 0;
    taskEXIT_CRITICAL(&s_awake_mux);
    return held;
}
