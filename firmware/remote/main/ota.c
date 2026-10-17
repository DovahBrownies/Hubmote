/*
 * Firmware updates over Wi-Fi (scripts/hm-ota.ps1), in developer builds
 * (CONFIG_HM_LOCAL_OTA). An HTTP server on CONFIG_HM_OTA_PORT takes POST
 * /ota with the app image as the body, signed with the update token (see
 * ota_authorized() in ota.h). It writes the image to the other app slot and
 * restarts into it. The new image must call ota_mark_good() once it has
 * reached Wi-Fi; otherwise the bootloader goes back to the old one on the
 * next restart (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
 * GET /version tells which build is running; GET /nonce hands out the
 * one-time value a signed request needs.
 *
 * In every build, ota_mark_good() confirms a freshly updated image.
 */
#include "ota.h"

#include <stdlib.h>
#include <string.h>
#include "build_info.h"
#include "devtools.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "ota";

#if CONFIG_HM_LOCAL_OTA

#define CHUNK 4096
#define RECV_TIMEOUTS 3 /* of recv_wait_timeout each: a stalled upload gives up */
#define NONCES        4
#define NONCE_US      (60 * 1000000LL)

static httpd_handle_t s_server;

static esp_err_t reply(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, text);
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500)); /* let the answer go out */
    esp_restart();
}

/* --- The token ------------------------------------------------------------------ */

#define NVS_NS       "ota"
#define NVS_TOKEN    "token"
#define TOKEN_LEN    20
#define TOKEN_LETTERS "abcdefghjkmnpqrstuvwxyz23456789" /* nothing that reads as another letter */

static char s_token[96];

static void load_token(void)
{
    const char *from = "saved";
    nvs_handle_t h;
    size_t len = sizeof(s_token);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, NVS_TOKEN, s_token, &len) != ESP_OK) {
            s_token[0] = '\0';
        }
        nvs_close(h);
    }
    if (!s_token[0]) {
        /* Made here, never built in: an image carrying the token would hand
         * it to anyone who sees an update go by. Wi-Fi is on by now, so
         * esp_random() is true randomness. */
        static const char letters[] = TOKEN_LETTERS;
        size_t n = 0;
        while (n < TOKEN_LEN) {
            uint8_t b = (uint8_t)esp_random();
            if (b < (256 / (sizeof(letters) - 1)) * (sizeof(letters) - 1)) { /* no modulo bias */
                s_token[n++] = letters[b % (sizeof(letters) - 1)];
            }
        }
        s_token[n] = '\0';
        from = "new";
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, NVS_TOKEN, s_token);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    ESP_LOGI(TAG, "update token: %s", from); /* never the token itself */
}

const char *ota_token(void)
{
    return s_token;
}

/* --- Signed requests ------------------------------------------------------------ */

/* The token never crosses the network: a request carries an HMAC of itself
 * and a one-time nonce, so a device posing as hubmote.local, or anyone
 * reading the traffic, learns nothing it could use for another request. */
static struct {
    char hex[33];
    int64_t at;
} s_nonces[NONCES]; /* the server's one task is the only user */
static size_t s_nonce_next;

static void to_hex(const uint8_t *b, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = digits[b[i] >> 4];
        out[2 * i + 1] = digits[b[i] & 15];
    }
    out[2 * n] = '\0';
}

static bool is_hex64(const char *s)
{
    size_t n = 0;
    for (; s[n]; n++) {
        if (!((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'a' && s[n] <= 'f'))) {
            return false;
        }
    }
    return n == 64;
}

static esp_err_t nonce_get(httpd_req_t *req)
{
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    to_hex(raw, sizeof(raw), s_nonces[s_nonce_next].hex);
    s_nonces[s_nonce_next].at = esp_timer_get_time();
    char out[34];
    snprintf(out, sizeof(out), "%s\n", s_nonces[s_nonce_next].hex);
    s_nonce_next = (s_nonce_next + 1) % NONCES;
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, out);
}

/* True if the nonce was handed out under NONCE_US ago; either way it can't
 * be used again. */
static bool take_nonce(const char *hex)
{
    for (size_t i = 0; i < NONCES; i++) {
        if (s_nonces[i].hex[0] && strcmp(s_nonces[i].hex, hex) == 0) {
            s_nonces[i].hex[0] = '\0';
            return esp_timer_get_time() - s_nonces[i].at < NONCE_US;
        }
    }
    return false;
}

/* HMAC-SHA256(token, nonce "\n" method "\n" uri "\n" body hash), as hex. */
static bool sign(const char *nonce, const char *method, const char *uri, const char *body, char out[65])
{
    const char *parts[] = {nonce, "\n", method, "\n", uri, "\n", body};
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    int ret = mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    if (ret == 0) {
        ret = mbedtls_md_hmac_starts(&md, (const unsigned char *)s_token, strlen(s_token));
    }
    for (size_t i = 0; ret == 0 && i < sizeof(parts) / sizeof(parts[0]); i++) {
        ret = mbedtls_md_hmac_update(&md, (const unsigned char *)parts[i], strlen(parts[i]));
    }
    uint8_t mac[32];
    if (ret == 0) {
        ret = mbedtls_md_hmac_finish(&md, mac);
    }
    mbedtls_md_free(&md);
    if (ret == 0) {
        to_hex(mac, sizeof(mac), out);
    }
    return ret == 0;
}

bool ota_authorized(httpd_req_t *req, char body_sha256[65])
{
    char nonce[40] = "", auth[72] = "", body[72] = "";
    bool ok = s_token[0] && httpd_req_get_hdr_value_str(req, "X-OTA-Nonce", nonce, sizeof(nonce)) == ESP_OK &&
              httpd_req_get_hdr_value_str(req, "X-OTA-Auth", auth, sizeof(auth)) == ESP_OK && is_hex64(auth) &&
              (!body_sha256 ||
               (httpd_req_get_hdr_value_str(req, "X-OTA-Body", body, sizeof(body)) == ESP_OK && is_hex64(body))) &&
              take_nonce(nonce);
    char expect[65];
    if (ok && sign(nonce, http_method_str((enum http_method)req->method), req->uri, body, expect)) {
        unsigned diff = 0;
        for (size_t i = 0; i < 64; i++) {
            diff |= (unsigned)(expect[i] ^ auth[i]); /* no early exit */
        }
        if (diff == 0) {
            if (body_sha256) {
                strlcpy(body_sha256, body, 65);
            }
            return true;
        }
    }
    ESP_LOGW(TAG, "%s refused: not signed with the update token", req->uri);
    vTaskDelay(pdMS_TO_TICKS(1000)); /* slows down guessing */
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_sendstr(req, "not signed with the update token (secrets.env OTA_TOKEN, Settings > About)\n");
    return false;
}

/* --- Updates ------------------------------------------------------------------- */

static esp_err_t ota_post(httpd_req_t *req)
{
    char want[65];
    if (!ota_authorized(req, want)) {
        return ESP_OK;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || req->content_len == 0 || req->content_len > part->size) {
        return reply(req, "400 Bad Request", "no update slot, or the image doesn't fit\n");
    }
    char *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    esp_ota_handle_t ota = 0;
    esp_err_t err = buf ? esp_ota_begin(part, req->content_len, &ota) : ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "receiving %u bytes into %s", (unsigned)req->content_len, part->label);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    int timeouts = 0;
    for (size_t left = req->content_len; err == ESP_OK && left > 0;) {
        int r = httpd_req_recv(req, buf, left < CHUNK ? left : CHUNK);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < RECV_TIMEOUTS) {
            continue;
        }
        if (r <= 0) {
            err = ESP_FAIL; /* upload cut short, or stalled */
            break;
        }
        timeouts = 0;
        mbedtls_sha256_update(&sha, (const unsigned char *)buf, (size_t)r);
        err = esp_ota_write(ota, buf, (size_t)r);
        left -= (size_t)r;
    }
    free(buf);
    uint8_t digest[32];
    char got[65];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    to_hex(digest, sizeof(digest), got);
    if (err == ESP_OK && strcmp(got, want) != 0) {
        ESP_LOGE(TAG, "the image isn't the one that was signed");
        err = ESP_ERR_INVALID_CRC;
    }
    if (err == ESP_OK) {
        err = esp_ota_end(ota); /* also checks the image */
        ota = 0;
    }
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(part);
    }
    if (err != ESP_OK) {
        if (ota) {
            esp_ota_abort(ota);
        }
        ESP_LOGE(TAG, "update failed: %s", esp_err_to_name(err));
        return reply(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "update written to %s; restarting", part->label);
    reply(req, "200 OK", "ok, restarting\n");
    xTaskCreate(restart_task, "ota_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t version_get(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    char json[192];
    snprintf(json, sizeof(json), "{\"project\":\"%s\",\"version\":\"%s\",\"built\":\"%s %s\",\"slot\":\"%s\"}\n",
             app->project_name, app->version, build_date(), build_time(), running ? running->label : "?");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

#else /* release build: no update server, no token */

const char *ota_token(void)
{
    return "";
}

#endif

void ota_start(void)
{
#if CONFIG_HM_LOCAL_OTA
    if (s_server) {
        return;
    }
    load_token();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = CONFIG_HM_OTA_PORT;
    cfg.ctrl_port = CONFIG_HM_OTA_PORT + 1;
    cfg.stack_size = 6144;
    cfg.recv_wait_timeout = 10;
    cfg.max_open_sockets = 3; /* one upload or tool at a time is plenty; sockets are scarce */
    cfg.lru_purge_enable = true; /* idle connections can't lock the tools out */
    cfg.backlog_conn = 2;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "server didn't start");
        s_server = NULL;
        return;
    }
    static const httpd_uri_t ota = {.uri = "/ota", .method = HTTP_POST, .handler = ota_post};
    static const httpd_uri_t version = {.uri = "/version", .method = HTTP_GET, .handler = version_get};
    static const httpd_uri_t nonce = {.uri = "/nonce", .method = HTTP_GET, .handler = nonce_get};
    httpd_register_uri_handler(s_server, &ota);
    httpd_register_uri_handler(s_server, &version);
    httpd_register_uri_handler(s_server, &nonce);
    devtools_register(s_server);
    ESP_LOGI(TAG, "updates on port %d", CONFIG_HM_OTA_PORT);
#endif
}

void ota_mark_good(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware confirmed (%s)", running->label);
    }
}
