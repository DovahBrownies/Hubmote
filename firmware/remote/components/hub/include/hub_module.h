#pragma once

/*
 * Modules: optional features reached from the Home tab (Weather, ...).
 *
 * A module is one ESP-IDF component in modules/<id>/. It fills in a
 * hub_module_t and registers it with HUB_MODULE(); nothing else in the
 * firmware has to change. Make one with tools/new_module.py and read
 * docs/modules.md.
 *
 * Threads:
 *  - init and event run on the controller task. Don't block there: use
 *    hub_http_get() for network work, or a task of your own.
 *  - create_view, show, hide, create_card and glance run on the LVGL task
 *    with the LVGL lock held. From any other task, reach the UI through
 *    hub_ui_async().
 */

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HUB_MODULE_ID_MAX 12 /* NVS namespaces are "m_<id>", 15 characters at most */
#define HUB_MODULES_MAX   24

/* The full-screen area a module's view gets, below the top bar. */
#define HUB_VIEW_W UI_WIDE_W /* ui_kit.h: 1000 at 1024 x 600 */
#define HUB_VIEW_H UI_BODY_H /* 532 at 1024 x 600 */

typedef enum {
    HUB_EV_NETWORK_UP,   /* Wi-Fi connected */
    HUB_EV_NETWORK_DOWN, /* Wi-Fi lost */
    HUB_EV_SCREEN_ON,    /* the screen woke up: refresh what is stale */
    HUB_EV_SCREEN_OFF,   /* the screen went dark: stop polling */
    HUB_EV_MINUTE,       /* once a minute */
} hub_event_t;

/* A one-line summary for the top bar and the switcher, e.g. a weather icon
 * and "18°". */
typedef struct {
    const char *icon;  /* an ICON_* string (font_icons_*), or NULL */
    lv_color_t color;  /* the icon's color */
    char text[16];
} hub_glance_t;

typedef struct {
    const char *id;   /* a lowercase letter, then letters, digits and '_'; HUB_MODULE_ID_MAX at most */
    const char *name; /* shown on the Home tab and in the switcher */
    const char *icon; /* an ICON_* string for lists */

    /* All callbacks are optional. */
    void (*init)(void);                /* once at start-up */
    void (*event)(hub_event_t event);

    lv_obj_t *(*create_view)(lv_obj_t *parent); /* its screen, HUB_VIEW_W x HUB_VIEW_H; built when first opened */
    void (*show)(void);                          /* its screen came into view */
    void (*hide)(void);                          /* its screen left */
    lv_obj_t *(*create_card)(lv_obj_t *parent); /* its card on the Home tab */
    bool (*glance)(hub_glance_t *out);          /* false: nothing to show yet */
} hub_module_t;

/* Registers a module before app_main() runs. Use once per module, at file
 * scope, after defining the hub_module_t:
 *     static const hub_module_t s_weather = { .id = "weather", ... };
 *     HUB_MODULE(s_weather);
 */
#define HUB_MODULE(module)                                        \
    __attribute__((constructor)) static void hub_register_##module(void) \
    {                                                             \
        hub_module_register(&(module));                           \
    }
void hub_module_register(const hub_module_t *module);

size_t hub_module_count(void);
const hub_module_t *hub_module_get(size_t index);
const hub_module_t *hub_module_find(const char *id);

/* Modules can be switched off in Settings (on by default; kept in NVS). An
 * off module stays in the firmware but gets no init or events and shows
 * nowhere: no card, switcher row or glance. */
bool hub_module_enabled(const hub_module_t *module);

/* For the platform (main/ and the UI), not for modules. */
void hub_modules_init(void);
void hub_modules_event(hub_event_t event);
/* Controller task. Switching a module on runs its init (the first time)
 * and tells it the network is up, if it is. */
void hub_module_set_enabled(const char *id, bool on);
/* Which modules are on changed. Any task. */
void hub_set_modules_listener(void (*listener)(void));

/* A module's glance changed; the top bar and switcher redraw it. Any task. */
void hub_glance_changed(void);
void hub_set_glance_listener(void (*listener)(void));

/* Runs fn(arg) on the LVGL task with the LVGL lock held. Any task. */
void hub_ui_async(void (*fn)(void *arg), void *arg);

#ifdef __cplusplus
}
#endif
