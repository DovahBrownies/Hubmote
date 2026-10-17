/* The module registry, events, glances and the hop onto the LVGL task. */
#include "hub_module.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "kv.h"

static const char *TAG = "hub";

#define NVS_OFF "hub_off" /* key = module id, present = switched off */

static const hub_module_t *s_modules[HUB_MODULES_MAX];
static size_t s_count;
static bool s_off[HUB_MODULES_MAX];
static bool s_started[HUB_MODULES_MAX];
static bool s_net_up;
static void (*s_glance_listener)(void);
static void (*s_modules_listener)(void);

/* Runs from a constructor, before app_main and before logging is set up:
 * only record the module here. */
void hub_module_register(const hub_module_t *module)
{
    if (module && module->id && s_count < HUB_MODULES_MAX) {
        s_modules[s_count++] = module;
    }
}

size_t hub_module_count(void)
{
    return s_count;
}

const hub_module_t *hub_module_get(size_t index)
{
    return index < s_count ? s_modules[index] : NULL;
}

const hub_module_t *hub_module_find(const char *id)
{
    for (size_t i = 0; id && i < s_count; i++) {
        if (strcmp(s_modules[i]->id, id) == 0) {
            return s_modules[i];
        }
    }
    return NULL;
}

static int index_of(const hub_module_t *module)
{
    for (size_t i = 0; module && i < s_count; i++) {
        if (s_modules[i] == module) {
            return (int)i;
        }
    }
    return -1;
}

bool hub_module_enabled(const hub_module_t *module)
{
    int i = index_of(module);
    return i >= 0 && !s_off[i];
}

static bool id_ok(const char *id)
{
    size_t n = strlen(id);
    if (n == 0 || n > HUB_MODULE_ID_MAX || id[0] < 'a' || id[0] > 'z') {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static int by_name(const void *a, const void *b)
{
    const hub_module_t *ma = *(const hub_module_t *const *)a;
    const hub_module_t *mb = *(const hub_module_t *const *)b;
    return strcmp(ma->name ? ma->name : ma->id, mb->name ? mb->name : mb->id);
}

/* init runs once, the first time a module is on. */
static void start(size_t i)
{
    if (!s_started[i]) {
        s_started[i] = true;
        if (s_modules[i]->init) {
            s_modules[i]->init();
        }
    }
}

void hub_modules_init(void)
{
    /* Constructors run in link order; sort so lists don't depend on it. */
    qsort(s_modules, s_count, sizeof(s_modules[0]), by_name);
    size_t kept = 0;
    for (size_t i = 0; i < s_count; i++) {
        bool taken = false;
        for (size_t j = 0; j < kept; j++) {
            taken = taken || strcmp(s_modules[j]->id, s_modules[i]->id) == 0;
        }
        if (!id_ok(s_modules[i]->id) || taken) {
            ESP_LOGE(TAG, "module id \"%s\" is invalid or taken; left out", s_modules[i]->id);
            continue;
        }
        s_modules[kept++] = s_modules[i];
    }
    s_count = kept;

    for (size_t i = 0; i < s_count; i++) {
        uint8_t v;
        s_off[i] = kv_get_u8(NVS_OFF, s_modules[i]->id, &v) == ESP_OK;
    }
    for (size_t i = 0; i < s_count; i++) {
        ESP_LOGI(TAG, "module %s%s", s_modules[i]->id, s_off[i] ? " (off)" : "");
        if (!s_off[i]) {
            start(i);
        }
    }
    if (s_modules_listener) {
        s_modules_listener();
    }
}

void hub_modules_event(hub_event_t event)
{
    if (event == HUB_EV_NETWORK_UP || event == HUB_EV_NETWORK_DOWN) {
        s_net_up = event == HUB_EV_NETWORK_UP;
    }
    for (size_t i = 0; i < s_count; i++) {
        if (!s_off[i] && s_started[i] && s_modules[i]->event) {
            s_modules[i]->event(event);
        }
    }
}

void hub_module_set_enabled(const char *id, bool on)
{
    int i = index_of(hub_module_find(id));
    if (i < 0 || s_off[i] == !on) {
        return;
    }
    s_off[i] = !on;
    if (on) {
        kv_erase(NVS_OFF, id);
    } else {
        kv_set_u8(NVS_OFF, id, 1);
    }
    ESP_LOGI(TAG, "module %s %s", id, on ? "on" : "off");
    if (on) {
        start((size_t)i);
        if (s_net_up && s_modules[i]->event) {
            s_modules[i]->event(HUB_EV_NETWORK_UP);
        }
    }
    if (s_modules_listener) {
        s_modules_listener();
    }
    hub_glance_changed();
}

void hub_set_modules_listener(void (*listener)(void))
{
    s_modules_listener = listener;
}

void hub_set_glance_listener(void (*listener)(void))
{
    s_glance_listener = listener;
}

void hub_glance_changed(void)
{
    if (s_glance_listener) {
        s_glance_listener();
    }
}

void hub_ui_async(void (*fn)(void *arg), void *arg)
{
    if (lvgl_port_lock(0)) {
        lv_async_call(fn, arg);
        lvgl_port_unlock();
    }
}
