#pragma once

/*
 * Developer tools on the update server (CONFIG_HM_DEV_TOOLS): a screenshot
 * and simulated touches, so the UI can be checked and driven without
 * looking at the remote (tools/devtools.py).
 */

#include "esp_http_server.h"

/* Adds the input device for simulated touches. Call once LVGL runs. */
void devtools_init(void);
/* Adds the /screenshot, /tap and /swipe handlers to the update server. */
void devtools_register(httpd_handle_t server);
