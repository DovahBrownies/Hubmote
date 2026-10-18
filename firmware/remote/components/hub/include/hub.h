#pragma once

/*
 * Everything a module needs from the platform, in one include:
 *   hub_module.h  registering a module, events, glances, UI thread
 *   hub_http.h    web requests
 *   hub_store.h   settings and files
 *   hub_time.h    wall-clock time
 *   hub_actions.h keys, apps and turning the TV off; what it is playing
 * The look (colors, fonts, icons, building blocks) is in ui_kit.h.
 */

#include "hub_actions.h"
#include "hub_http.h"
#include "hub_module.h"
#include "hub_store.h"
#include "hub_time.h"
