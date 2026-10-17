#pragma once

/* Firmware updates over Wi-Fi (main/ota.c, scripts/hm-ota.ps1), and
 * confirming a freshly updated image. */

#include <stdbool.h>
#include <stddef.h>
#include "esp_http_server.h"

/* In developer builds (CONFIG_HM_LOCAL_OTA), starts the update server; call
 * once the network is up (safe to call again). Release builds: nothing. */
void ota_start(void);
/* Confirms a freshly updated image so the bootloader keeps it; call once
 * it has proven itself (reached Wi-Fi). Does nothing otherwise. */
void ota_mark_good(void);

/* The token updates and developer tools are signed with: made up on the
 * remote the first time and kept in NVS (namespace ota); Settings > About
 * shows it, for OTA_TOKEN in the PC's secrets.env. "" until ota_start()
 * has run, and always in release builds. */
const char *ota_token(void);
/* True if the request is signed with the token; otherwise answers 403
 * itself (after a second). A signed request carries
 *   X-OTA-Nonce: a value from GET /nonce (used once, for 60 s at most)
 *   X-OTA-Auth:  hex HMAC-SHA256(token, nonce "\n" method "\n" uri "\n" body)
 * where uri is the path with its query and body is "" or, for a request
 * with a body (body_sha256 not NULL), the hex SHA-256 of the body, also
 * sent as X-OTA-Body and copied to body_sha256: the handler must check the
 * body against it. Hex is lowercase. tools/hm_auth.py does the client side. */
bool ota_authorized(httpd_req_t *req, char body_sha256[65]);
