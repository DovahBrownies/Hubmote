#pragma once

/* Firmware updates over Wi-Fi (main/ota.c, scripts/hm-ota.ps1), and
 * confirming a freshly updated image. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
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

/* Reads the next part of an image into buf, at most len bytes: returns how
 * many it read, or 0 or less when the data stopped. */
typedef int (*ota_read_t)(void *ctx, uint8_t *buf, size_t len);
/* Called after each part with how much of the image is written. */
typedef void (*ota_progress_t)(void *ctx, size_t done, size_t size);

/* Writes an image of size bytes into the spare app slot, reading it with
 * read(ctx) and telling progress(ctx) (may be NULL). Every build has it:
 * the update server and updates from the releases (update.c) both use it.
 * It checks the image against sha256 (hex, either case) and checks the
 * image itself, then makes it the next boot; restart to run it. Errors:
 * ESP_ERR_INVALID_SIZE (no spare slot, or the image doesn't fit), ESP_FAIL
 * (the data stopped early), ESP_ERR_INVALID_CRC (a different image), and
 * the esp_ota_* errors. */
esp_err_t ota_write_image(size_t size, const char *sha256, ota_read_t read, ota_progress_t progress, void *ctx);
