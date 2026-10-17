/*
 * Updates from the published releases. The web installer's site (GitHub
 * Pages, or a PC with scripts/hm-web.ps1 -Serve) carries update.json:
 *
 *   {"version":"0.1.1","built":"Oct 20 2026 18:02",
 *    "boards":{"waveshare_s3_touch_lcd_7b":{"image":"waveshare_s3_touch_lcd_7b/hubmote.bin",
 *                                           "size":1971872,"sha256":"..."}, ...},
 *    "companion":{"version":"0.5","code":5,"apk":"companion-0.5.apk",
 *                 "size":61234,"sha256":"..."}}
 *
 * The remote compares the version with its own (a release beats its own
 * pre-releases: 0.1.0 is newer than 0.1.0-dev), and Install downloads its
 * board's image (CONFIG_HM_BOARD_ID) into the spare app slot, checking its
 * size and SHA-256, then restarts into it. An image that doesn't reach
 * Wi-Fi is rolled back (ota_mark_good()). It never installs by itself.
 *
 * HTTPS only, checked against ESP-IDF's CA bundle; developer builds also
 * accept http:// (a site served from a PC, for testing).
 *
 * "companion" (optional) is the companion app of the same release: the
 * controller compares it with the one on the TV, and the companion
 * downloads it itself (companion_update()).
 */
#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota.h"
#include "sdkconfig.h"

static const char *TAG = "update";

#define BIT_CHECK      (1u << 0)
#define BIT_INSTALL    (1u << 1)
#define BIT_NET        (1u << 2)
#define FIRST_CHECK_US (60LL * 1000000)
#define CHECK_EVERY_US (24LL * 3600 * 1000000)
#define JSON_MAX       8192 /* update.json's size limit: tools/web/make_site.py checks it */
#define TASK_STACK     8192 /* TLS; internal RAM, as it writes flash */

static TaskHandle_t s_task;
static update_cb_t s_cb;
static update_status_t s_status;
static char s_image[64]; /* from update.json */
static int s_size;
static char s_sha256[65];

static void report(update_state_t state, int percent)
{
    s_status.state = state;
    s_status.percent = percent;
    if (s_cb) {
        s_cb(&s_status);
    }
}

/* --- Versions ------------------------------------------------------------------ */

/* "1.2.3" or "1.2.3-pre" -> the numbers and the part after '-' (or ""). */
static bool parse_version(const char *v, int n[3], const char **pre)
{
    if (sscanf(v, "%d.%d.%d", &n[0], &n[1], &n[2]) != 3) {
        return false;
    }
    const char *dash = strchr(v, '-');
    *pre = dash ? dash + 1 : "";
    return true;
}

/* <0 if a is older than b, 0 if the same, >0 if newer. A release is newer
 * than its own pre-releases; versions that don't parse count as older. */
static int compare_versions(const char *a, const char *b)
{
    int na[3], nb[3];
    const char *pa, *pb;
    bool oka = parse_version(a, na, &pa);
    bool okb = parse_version(b, nb, &pb);
    if (!oka || !okb) {
        return oka - okb;
    }
    for (int i = 0; i < 3; i++) {
        if (na[i] != nb[i]) {
            return na[i] < nb[i] ? -1 : 1;
        }
    }
    if (!pa[0] || !pb[0]) {
        return (pa[0] ? -1 : 0) + (pb[0] ? 1 : 0);
    }
    return strcmp(pa, pb);
}

/* --- HTTP -------------------------------------------------------------------- */

static bool url_allowed(const char *url)
{
#if CONFIG_HM_DEVELOPER
    return strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0;
#else
    return strncmp(url, "https://", 8) == 0;
#endif
}

/* An open GET with headers read, or NULL. *length: Content-Length (-1
 * unknown); *status: the HTTP status, or -1 without an answer. */
static esp_http_client_handle_t open_get(const char *url, int64_t *length, int *status)
{
    *status = -1;
    if (!url_allowed(url)) {
        ESP_LOGW(TAG, "not an allowed address (https only): %s", url);
        return NULL;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return NULL;
    }
    esp_err_t err = esp_http_client_open(c, 0);
    if (err == ESP_OK) {
        *length = esp_http_client_fetch_headers(c);
        *status = esp_http_client_get_status_code(c);
        if (*status == 200) {
            return c;
        }
        if (*status != 404) {
            ESP_LOGW(TAG, "GET %s: HTTP %d", url, *status);
        }
    } else {
        ESP_LOGW(TAG, "GET %s: %s", url, esp_err_to_name(err));
    }
    esp_http_client_cleanup(c);
    return NULL;
}

void update_file_url(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s%s", CONFIG_HM_UPDATE_URL, name);
}

static bool is_sha256(const char *s)
{
    if (strlen(s) != 64) {
        return false;
    }
    for (; *s; s++) {
        if (!strchr("0123456789abcdefABCDEF", *s)) {
            return false;
        }
    }
    return true;
}

/* A file on the site, below update.json: letters, digits, '.', '_', '-'
 * and '/' between names, no name starting with a dot, so the URL can't
 * point anywhere else. */
static bool name_ok(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n == 0 || n >= max || s[n - 1] == '/') {
        return false;
    }
    for (const char *p = s; *p; p++) {
        bool starts_name = p == s || p[-1] == '/';
        if (starts_name && (*p == '.' || *p == '/')) {
            return false;
        }
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' ||
              *p == '_' || *p == '-' || *p == '/')) {
            return false;
        }
    }
    return true;
}

/* update.json's "companion", or an empty one if it has none (or it is unusable). */
static void read_companion(const cJSON *root, update_companion_t *out)
{
    memset(out, 0, sizeof(*out));
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "companion");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(c, "version");
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(c, "code");
    const cJSON *apk = cJSON_GetObjectItemCaseSensitive(c, "apk");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(c, "size");
    const cJSON *sha = cJSON_GetObjectItemCaseSensitive(c, "sha256");
    if (!cJSON_IsObject(c)) {
        return;
    }
    bool ok = cJSON_IsString(version) && strlen(version->valuestring) < sizeof(out->version) &&
              cJSON_IsNumber(code) && code->valueint > 0 && cJSON_IsString(apk) &&
              name_ok(apk->valuestring, sizeof(out->apk)) && cJSON_IsNumber(size) && size->valueint > 0 && cJSON_IsString(sha) && is_sha256(sha->valuestring);
    if (!ok) {
        ESP_LOGW(TAG, "update.json: companion entry not usable");
        return;
    }
    strlcpy(out->version, version->valuestring, sizeof(out->version));
    out->code = code->valueint;
    strlcpy(out->apk, apk->valuestring, sizeof(out->apk));
    out->size = size->valueint;
    strlcpy(out->sha256, sha->valuestring, sizeof(out->sha256));
}

/* --- Check ------------------------------------------------------------------- */

static bool check(void)
{
    report(UPDATE_CHECKING, 0);
    char url[256];
    update_file_url(url, sizeof(url), "update.json");
    int64_t length = -1;
    int status;
    esp_http_client_handle_t c = open_get(url, &length, &status);
    if (status == 404) {
        /* Nothing published there yet (before the first release): nothing to install. */
        ESP_LOGI(TAG, "no release published at %s", CONFIG_HM_UPDATE_URL);
        s_status.version[0] = '\0';
        memset(&s_status.companion, 0, sizeof(s_status.companion));
        report(UPDATE_CURRENT, 0);
        return false;
    }
    char *json = c ? heap_caps_calloc(1, JSON_MAX + 1, MALLOC_CAP_SPIRAM) : NULL;
    int got = 0;
    while (json && got < JSON_MAX) {
        int r = esp_http_client_read(c, json + got, JSON_MAX - got);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    if (c) {
        esp_http_client_cleanup(c);
    }
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    free(json);
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *boards = cJSON_GetObjectItemCaseSensitive(root, "boards");
    const cJSON *board = cJSON_GetObjectItemCaseSensitive(boards, CONFIG_HM_BOARD_ID);
    const cJSON *image = cJSON_GetObjectItemCaseSensitive(board, "image");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(board, "size");
    const cJSON *sha = cJSON_GetObjectItemCaseSensitive(board, "sha256");
    if (cJSON_IsString(version) && cJSON_IsObject(boards) && !board) {
        /* A release without this board: no firmware to install, but its
         * companion app still applies. */
        ESP_LOGW(TAG, "release %s has no image for %s", version->valuestring, CONFIG_HM_BOARD_ID);
        s_status.version[0] = '\0';
        read_companion(root, &s_status.companion);
        cJSON_Delete(root);
        report(UPDATE_CURRENT, 0);
        return false;
    }
    bool ok = cJSON_IsString(version) && cJSON_IsString(image) && cJSON_IsNumber(size) && size->valueint > 0 &&
              name_ok(image->valuestring, sizeof(s_image)) && strlen(version->valuestring) < sizeof(s_status.version) &&
              cJSON_IsString(sha) && is_sha256(sha->valuestring);
    if (ok) {
        strlcpy(s_status.version, version->valuestring, sizeof(s_status.version));
        strlcpy(s_image, image->valuestring, sizeof(s_image));
        s_size = size->valueint;
        strlcpy(s_sha256, sha->valuestring, sizeof(s_sha256));
        read_companion(root, &s_status.companion);
    }
    cJSON_Delete(root);
    if (!ok) {
        ESP_LOGW(TAG, "no usable update.json at %s", url);
        report(UPDATE_FAILED, 0);
        return false;
    }
    const char *mine = esp_app_get_description()->version;
    bool newer = compare_versions(mine, s_status.version) < 0;
    ESP_LOGI(TAG, "release %s, running %s: %s%s%s", s_status.version, mine, newer ? "update available" : "up to date",
             s_status.companion.version[0] ? "; companion " : "", s_status.companion.version);
    report(newer ? UPDATE_AVAILABLE : UPDATE_CURRENT, 0);
    return newer;
}

/* --- Install ----------------------------------------------------------------- */

/* The download, read for ota_write_image(), with progress in 5 % steps. */
typedef struct {
    esp_http_client_handle_t client;
    int shown;
} download_t;

static int download_read(void *ctx, uint8_t *buf, size_t len)
{
    download_t *d = ctx;
    return esp_http_client_read(d->client, (char *)buf, (int)len);
}

static void download_progress(void *ctx, size_t done, size_t size)
{
    download_t *d = ctx;
    int percent = (int)((uint64_t)done * 100 / size);
    if (percent >= d->shown + 5) {
        d->shown = percent;
        report(UPDATE_INSTALLING, percent < 100 ? percent : 99);
    }
}

static void install(void)
{
    if (s_status.state != UPDATE_AVAILABLE && !check()) {
        return;
    }
    report(UPDATE_INSTALLING, 0);
    char url[256];
    update_file_url(url, sizeof(url), s_image);
    int64_t length = -1;
    int status;
    download_t download = {.client = open_get(url, &length, &status)};
    esp_err_t err = !download.client                     ? ESP_FAIL
                    : (length >= 0 && length != s_size) ? ESP_ERR_INVALID_SIZE
                                                        : ESP_OK;
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "installing %s (%d bytes)", s_status.version, s_size);
        err = ota_write_image((size_t)s_size, s_sha256, download_read, download_progress, &download);
        if (err == ESP_ERR_INVALID_CRC) {
            ESP_LOGE(TAG, "SHA-256 doesn't match update.json");
        }
    }
    if (download.client) {
        esp_http_client_cleanup(download.client);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "install failed: %s", esp_err_to_name(err));
        report(UPDATE_FAILED, 0);
        return;
    }
    ESP_LOGI(TAG, "installed %s; restarting", s_status.version);
    report(UPDATE_INSTALLING, 100);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/* --- Task -------------------------------------------------------------------- */

static void task(void *arg)
{
    int64_t next_auto = 0; /* 0: no automatic check planned (no Wi-Fi yet, or off) */
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (next_auto) {
            int64_t left = next_auto - esp_timer_get_time();
            wait = left > 0 ? pdMS_TO_TICKS(left / 1000) + 1 : 0;
        }
        uint32_t bits = 0;
        bool woken = xTaskNotifyWait(0, UINT32_MAX, &bits, wait) == pdTRUE;
        if ((bits & BIT_NET) && CONFIG_HM_UPDATE_AUTO && !next_auto) {
            next_auto = esp_timer_get_time() + FIRST_CHECK_US;
        }
        if (bits & BIT_INSTALL) {
            install();
        } else if (bits & BIT_CHECK) {
            check();
        } else if (!woken && next_auto) {
            check();
            next_auto = esp_timer_get_time() + CHECK_EVERY_US;
        }
    }
}

void update_start(update_cb_t cb)
{
    if (s_task) {
        return;
    }
    s_cb = cb;
    if (xTaskCreate(task, "update", TASK_STACK, NULL, 3, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "no task");
        s_task = NULL;
    }
}

static void notify(uint32_t bit)
{
    if (s_task) {
        xTaskNotify(s_task, bit, eSetBits);
    }
}

void update_net_up(void)
{
    notify(BIT_NET);
}

void update_check(void)
{
    notify(BIT_CHECK);
}

void update_install(void)
{
    notify(BIT_INSTALL);
}
