/*
 * One task runs modules' web requests in turn. Each request gets a fresh
 * connection, closed when it ends: nothing holds a TLS session (and its
 * internal RAM) between requests.
 */
#include "hub_http.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "hub_http";

#define QUEUE_LEN  8
#define STACK      8192
#define TIMEOUT_MS 15000

typedef struct {
    char *url;
    size_t max_len;
    hub_http_done_t done;
    void *ctx;
} request_t;

static QueueHandle_t s_queue;

static void run(const request_t *r)
{
    int status = -1;
    size_t len = 0;
    char *body = heap_caps_malloc(r->max_len + 1, MALLOC_CAP_SPIRAM);
    esp_http_client_config_t cfg = {
        .url = r->url,
        .timeout_ms = TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "Hubmote",
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = body ? esp_http_client_init(&cfg) : NULL;
    if (client && esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        while (len < r->max_len) {
            int n = esp_http_client_read(client, body + len, (int)(r->max_len - len));
            if (n <= 0) {
                break;
            }
            len += (size_t)n;
        }
    }
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (body) {
        body[len] = '\0';
    }
    ESP_LOGI(TAG, "GET %.*s... -> %d, %u bytes", 48, r->url, status, (unsigned)len);
    r->done(status, body ? body : "", len, r->ctx);
    free(body);
}

static void http_task(void *arg)
{
    QueueHandle_t queue = arg; /* s_queue is set only once this task exists */
    request_t r;
    for (;;) {
        if (xQueueReceive(queue, &r, portMAX_DELAY) == pdTRUE) {
            run(&r);
            free(r.url);
        }
    }
}

esp_err_t hub_http_get(const char *url, size_t max_len, hub_http_done_t done, void *ctx)
{
    if (!url || !done || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The worker starts with the first request, from whichever task: made
     * once, and a queue only kept with a task to read it. */
    static portMUX_TYPE s_start_mux = portMUX_INITIALIZER_UNLOCKED;
    static bool s_starting;
    taskENTER_CRITICAL(&s_start_mux);
    bool start = !s_queue && !s_starting;
    s_starting |= start;
    taskEXIT_CRITICAL(&s_start_mux);
    if (start) {
        QueueHandle_t q = xQueueCreate(QUEUE_LEN, sizeof(request_t));
        if (q && xTaskCreatePinnedToCore(http_task, "hub_http", STACK, q, 4, NULL, 0) == pdPASS) {
            s_queue = q;
        } else if (q) {
            vQueueDelete(q);
        }
        s_starting = false;
    }
    if (!s_queue) {
        return ESP_ERR_NO_MEM; /* or still starting on another task: try again */
    }
    request_t r = {.url = heap_caps_malloc(strlen(url) + 1, MALLOC_CAP_SPIRAM), .max_len = max_len,
                   .done = done, .ctx = ctx};
    if (!r.url) {
        return ESP_ERR_NO_MEM;
    }
    strcpy(r.url, url);
    if (xQueueSend(s_queue, &r, 0) != pdTRUE) {
        free(r.url);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

size_t hub_url_encode(const char *text, char *out, size_t out_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                     *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (n + (plain ? 1 : 3) >= out_len) {
            return 0;
        }
        if (plain) {
            out[n++] = (char)*p;
        } else {
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 15];
        }
    }
    out[n] = '\0';
    return n;
}
