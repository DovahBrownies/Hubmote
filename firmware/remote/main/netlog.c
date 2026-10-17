/*
 * Log over Wi-Fi. Every log line also goes into a ring buffer in PSRAM, and
 * a small TCP server streams it to one client (scripts/hm-netlog.ps1). A new
 * client first gets the backlog, so the boot log isn't lost. The PC connects
 * out to the remote, so nothing has to be opened in its firewall.
 */
#include "netlog.h"

#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#if CONFIG_HM_NETLOG

#define BACKLOG    (32 * 1024)
#define CHUNK      1024
#define NETLOG_LINE_MAX   384
#define SEND_MS    2000

static char *s_ring;
static size_t s_total; /* bytes ever written; the ring holds the last BACKLOG */
static char s_line[NETLOG_LINE_MAX];
static SemaphoreHandle_t s_lock;
static vprintf_like_t s_console;
static TaskHandle_t s_task;

static void ring_append(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        s_ring[(s_total + i) % BACKLOG] = p[i];
    }
    s_total += n;
}

/* Formats once into s_line, then writes it to the console and the ring. If
 * the lock is busy, the line goes to the console only; a logging task never
 * waits more than a moment. */
static int log_hook(const char *fmt, va_list args)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) {
        return s_console(fmt, args);
    }
    int n = vsnprintf(s_line, sizeof(s_line), fmt, args);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(s_line) ? (size_t)n : sizeof(s_line) - 1;
        fwrite(s_line, 1, len, stdout);
        ring_append(s_line, len);
    }
    xSemaphoreGive(s_lock);
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
    return n;
}

/* Copies what the client hasn't had yet (from *sent) into buf. */
static size_t take(size_t *sent, char *buf, size_t max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_total - *sent > BACKLOG) {
        *sent = s_total - BACKLOG; /* the client fell behind: skip to what's still there */
    }
    size_t n = s_total - *sent;
    n = n < max ? n : max;
    for (size_t i = 0; i < n; i++) {
        buf[i] = s_ring[(*sent + i) % BACKLOG];
    }
    xSemaphoreGive(s_lock);
    *sent += n;
    return n;
}

static void serve(int client, int listener)
{
    static char chunk[CHUNK];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t sent = s_total > BACKLOG ? s_total - BACKLOG : 0;
    xSemaphoreGive(s_lock);
    for (;;) {
        size_t n;
        while ((n = take(&sent, chunk, sizeof(chunk))) > 0) {
            if (send(client, chunk, n, 0) != (int)n) {
                return;
            }
        }
        /* Wait for new lines; meanwhile notice a closed client or a newer one. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client, &rfds);
        FD_SET(listener, &rfds);
        struct timeval tv = {0};
        if (select((client > listener ? client : listener) + 1, &rfds, NULL, NULL, &tv) > 0) {
            char c;
            if (FD_ISSET(listener, &rfds) || (FD_ISSET(client, &rfds) && recv(client, &c, 1, 0) <= 0)) {
                return; /* a newer client replaces this one, or this one closed */
            }
        }
    }
}

static void server_task(void *arg)
{
    for (;;) {
        int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(CONFIG_HM_NETLOG_PORT)};
        int one = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (listener < 0 || bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 1) != 0) {
            if (listener >= 0) {
                close(listener);
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        for (;;) {
            int client = accept(listener, NULL, NULL);
            if (client < 0) {
                break;
            }
            struct timeval tv = {.tv_sec = SEND_MS / 1000, .tv_usec = (SEND_MS % 1000) * 1000};
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            serve(client, listener);
            close(client);
        }
        close(listener);
    }
}

void netlog_init(void)
{
    s_ring = heap_caps_calloc(1, BACKLOG, MALLOC_CAP_SPIRAM);
    s_lock = xSemaphoreCreateMutex();
    if (!s_ring || !s_lock) {
        return;
    }
    s_console = esp_log_set_vprintf(log_hook);
}

void netlog_serve(void)
{
    if (s_ring && !s_task) {
        xTaskCreate(server_task, "netlog", 3072, NULL, 2, &s_task);
    }
}

#else

void netlog_init(void)
{
}

void netlog_serve(void)
{
}

#endif
