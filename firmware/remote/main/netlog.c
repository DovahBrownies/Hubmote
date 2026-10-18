/*
 * Log over Wi-Fi. Every log line also goes into a ring buffer in PSRAM, and
 * a small TCP server streams it to one client (scripts/hm-netlog.ps1). A new
 * client first gets the backlog, so the boot log isn't lost. The PC connects
 * out to the remote, so nothing has to be opened in its firewall.
 *
 * Only a client with the update token (main/ota.h) gets the log. The remote
 * opens with a challenge, the client answers with an HMAC of it, and the
 * token never crosses the network:
 *
 *   remote: "hubmote-log <nonce>\n"            (32 hex digits, new each time)
 *   client: hex HMAC-SHA256(token, "hubmote-log\n" nonce) "\n"
 *   remote: the log, or "refused\n" and it closes
 *
 * The log itself isn't encrypted. A newer client replaces the current one
 * only once it has answered.
 */
#include "netlog.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/md.h"
#include "ota.h"
#include "sdkconfig.h"

#if CONFIG_HM_NETLOG

#define BACKLOG    (32 * 1024)
#define CHUNK      1024
#define NETLOG_LINE_MAX   384
#define SEND_MS    2000
#define ANSWER_MS  5000 /* for the client's answer to the challenge */

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
        if ((size_t)n >= sizeof(s_line)) {
            s_line[len - 1] = '\n'; /* cut short: keep its line break, or the next line joins it */
        }
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

static void to_hex(const uint8_t *b, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = digits[b[i] >> 4];
        out[2 * i + 1] = digits[b[i] & 15];
    }
    out[2 * n] = '\0';
}

/* Sends the challenge and checks the answer (see the top of this file). */
static bool authorized(int client)
{
    const char *token = ota_token();
    if (!token[0]) {
        return false; /* not loaded yet, or no update server: nobody gets the log */
    }
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    char nonce[33], hello[48];
    to_hex(raw, sizeof(raw), nonce);
    int n = snprintf(hello, sizeof(hello), "hubmote-log %s\n", nonce);
    if (send(client, hello, (size_t)n, 0) != n) {
        return false;
    }

    char answer[66]; /* 64 hex digits and the line end */
    size_t got = 0;
    int64_t until = (int64_t)xTaskGetTickCount() + pdMS_TO_TICKS(ANSWER_MS);
    while (got < sizeof(answer) && (got == 0 || answer[got - 1] != '\n')) {
        int64_t left = until - (int64_t)xTaskGetTickCount();
        if (left <= 0) {
            return false;
        }
        int ms = (int)(left * portTICK_PERIOD_MS);
        struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int r = recv(client, answer + got, sizeof(answer) - got, 0);
        if (r <= 0) {
            return false;
        }
        got += (size_t)r;
    }
    if (got < 65 || answer[64] != '\n') {
        return false;
    }

    static const char label[] = "hubmote-log\n";
    uint8_t mac[32];
    char expect[65];
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    int ret = mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    if (ret == 0) {
        ret = mbedtls_md_hmac_starts(&md, (const unsigned char *)token, strlen(token));
    }
    if (ret == 0) {
        ret = mbedtls_md_hmac_update(&md, (const unsigned char *)label, sizeof(label) - 1);
    }
    if (ret == 0) {
        ret = mbedtls_md_hmac_update(&md, (const unsigned char *)nonce, strlen(nonce));
    }
    if (ret == 0) {
        ret = mbedtls_md_hmac_finish(&md, mac);
    }
    mbedtls_md_free(&md);
    if (ret != 0) {
        return false;
    }
    to_hex(mac, sizeof(mac), expect);
    unsigned diff = 0;
    for (size_t i = 0; i < 64; i++) {
        diff |= (unsigned)(expect[i] ^ answer[i]); /* no early exit */
    }
    return diff == 0;
}

/* Accepts a client and lets it in if it answers the challenge: the client,
 * -1 if it didn't answer, -2 if the listener broke. */
static int accept_client(int listener)
{
    int client = accept(listener, NULL, NULL);
    if (client < 0) {
        return -2;
    }
    struct timeval tv = {.tv_sec = SEND_MS / 1000, .tv_usec = (SEND_MS % 1000) * 1000};
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (!authorized(client)) {
        static const char no[] = "refused\n";
        send(client, no, sizeof(no) - 1, 0);
        close(client);
        return -1;
    }
    struct sockaddr_in peer;
    socklen_t len = sizeof(peer);
    char ip[16] = "?";
    if (getpeername(client, (struct sockaddr *)&peer, &len) == 0) {
        inet_ntoa_r(peer.sin_addr, ip, sizeof(ip));
    }
    ESP_LOGI("netlog", "log client %s connected (stack left %u)", ip, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return client;
}

/* Streams the log to client until it closes: -1, or until a newer client
 * answers the challenge: that client. -2 if the listener broke. */
static int serve(int client, int listener)
{
    static char chunk[CHUNK];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t sent = s_total > BACKLOG ? s_total - BACKLOG : 0;
    xSemaphoreGive(s_lock);
    for (;;) {
        size_t n;
        while ((n = take(&sent, chunk, sizeof(chunk))) > 0) {
            if (send(client, chunk, n, 0) != (int)n) {
                return -1;
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
            if (FD_ISSET(client, &rfds) && recv(client, &c, 1, 0) <= 0) {
                return -1; /* this one closed */
            }
            if (FD_ISSET(listener, &rfds)) {
                int next = accept_client(listener);
                if (next != -1) {
                    return next; /* a newer client replaces this one */
                }
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
        int client = -1;
        while (client != -2) {
            if (client < 0) {
                client = accept_client(listener);
                continue;
            }
            int next = serve(client, listener);
            close(client);
            client = next;
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
        xTaskCreate(server_task, "netlog", 4096, NULL, 2, &s_task); /* the login (HMAC) can run inside serve() */
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
