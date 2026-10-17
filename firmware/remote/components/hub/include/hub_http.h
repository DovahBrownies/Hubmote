#pragma once

/*
 * Web requests for modules. All requests share one task and run one at a
 * time, so modules never hold their own TLS session (internal RAM is the
 * scarce resource on this board). HTTPS uses ESP-IDF's certificate bundle.
 */

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called on the HTTP task when a request ends. status is the HTTP status,
 * or -1 when the request failed before one arrived. body is NUL-terminated
 * (never NULL) and freed after the call returns. */
typedef void (*hub_http_done_t)(int status, const char *body, size_t len, void *ctx);

/* Queues a GET of url. Bodies longer than max_len are cut short (status
 * stays, len == max_len). */
esp_err_t hub_http_get(const char *url, size_t max_len, hub_http_done_t done, void *ctx);

/* Percent-encodes text for a URL query. Returns the length written, or 0
 * if out is too small. */
size_t hub_url_encode(const char *text, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
