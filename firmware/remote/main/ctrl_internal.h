#pragma once

/*
 * The controller's parts. controller.c owns the message queue and the task,
 * and hands each message to the part it concerns:
 *
 *   ctrl_setup.c      first-run flow: Wi-Fi, finding and pairing the TV
 *   ctrl_companion.c  the companion app (apps, pairing, updates) and typing
 *   ctrl_screen.c     Settings > Screen and Appearance, dimming, turning
 *                     off, the battery
 *
 * Everything runs on the controller task, so the parts keep their own state
 * without locks. A part's *_ui() handles the UI actions that are its own and
 * returns false for the others.
 */

#include <stdbool.h>
#include <stdint.h>
#include "atv_remote.h"
#include "controller.h"
#include "companion.h"
#include "net.h"
#include "ui.h"
#include "update.h"


/* A UI action as queued (ui_action_t with its strings copied). */
typedef struct {
    ui_action_type_t type;
    ui_target_t target;
    int key;
    ui_dir_t dir;
    int index;
    char text[128];
    char name[36]; /* fits a 32-byte Wi-Fi name */
} ctrl_ui_t;

/* --- ctrl_setup.c ------------------------------------------------------------------ */

/* The TV's saved name; call before anything connects. */
void ctrl_setup_load(void);
/* Creates the pairing identity if needed, then starts Wi-Fi (net events go
 * to on_net). */
void ctrl_setup_start(net_cb_t on_net);
/* Connection state, pairing results and the TV's name. */
void ctrl_setup_atv(const atv_event_t *e);
/* Wi-Fi came up or went down (after controller.c's own duties). */
void ctrl_setup_net(bool up, int reason);
bool ctrl_setup_ui(const ctrl_ui_t *a);

/* --- ctrl_companion.c -------------------------------------------------------------- */

void ctrl_companion_start(companion_cb_t on_event);
void ctrl_companion_event(const companion_event_t *e);
/* A release check finished: its companion (version "" if none). */
void ctrl_companion_release(const update_companion_t *c);
bool ctrl_companion_ui(const ctrl_ui_t *a);
/* Opens an app on the TV through the companion (a module asked). */
void ctrl_companion_launch(const char *package);
/* Runs a Google TV search once typing has paused. */
void ctrl_companion_tick(int64_t now);

/* --- ctrl_screen.c ----------------------------------------------------------------- */

/* Reads Settings > Screen and the theme from NVS; call before ui_init(). */
void ctrl_screen_load(void);
/* Shows them in Settings; call after ui_init(). */
void ctrl_screen_show(void);
/* The TV connected: reopens Settings > Appearance once after a theme restart. */
void ctrl_screen_connected(void);
/* Dimming, screen off, turning off after idle hours, the battery. */
void ctrl_screen_tick(int64_t now);
bool ctrl_screen_ui(const ctrl_ui_t *a);
