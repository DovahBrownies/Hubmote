#include "companion.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "miniz.h" /* the ROM's deflate and inflate */
#include "kv.h"

static const char *TAG = "companion";

#define PORT               6470
#define CONNECT_TIMEOUT_MS 3000
#define IO_TIMEOUT_MS      5000
#define REQUEST_MS         8000 /* a whole answer's headers (and body) */
#define HEADER_BYTES_MAX   4096
#define EVENTS_SHORT_US    (10 * 1000000LL) /* a stream that ends sooner backs off */
#define EVENTS_IDLE_MS     40000 /* the app sends an empty line every 15 s */
#define REFRESH_EVERY_US   (30LL * 60 * 1000000)
#define RETRY_MIN_MS       5000
#define RETRY_MAX_MS       60000
#define LIST_MAX_BYTES     (64 * 1024)
#define TASK_STACK         6144
#define DEFLATE_STACK      10240 /* the ROM's deflate uses about 7.1 KB (measured): a task of its own */
#define TOKEN_MAX          64
#define NVS_NS             "companion"
#define NVS_TOKEN          "token"
#define CACHE_MAGIC        0x43494D48u /* "HMIC" */
#define ICON_BYTES(px)     ((size_t)(px) * (px) * 3)

/* An icon no list uses any more, freed once the UI shows list gen or newer. */
typedef struct {
    uint8_t *buf;
    uint32_t gen;
} stale_t;

static struct {
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    companion_cb_t cb;
    void *ctx;
    char host[40];
    char token[TOKEN_MAX + 1]; /* from pairing; "" until then */
    int icon_px;       /* the home tile's icon size */
    int icon_small_px; /* the app panel's */
    int events_fd;
    bool online;
    bool needs_pairing; /* the companion answered 401: pair in Settings */
    companion_field_t field;
    int field_id;
    int version; /* from /status while online; 0 = unknown */
    char version_name[COMPANION_VERSION];
    bool can_update;
    companion_update_t update;
    bool media_known; /* the companion reads media sessions (/status "media") */
    companion_media_t media; /* under lock */
    companion_speed_t speed; /* under lock */
    companion_app_t *apps; /* written by the task only; copied out under lock */
    size_t count;
    uint32_t gen;
    stale_t *stale;
    size_t stale_count;
    size_t stale_cap;
    char cache_dir[48]; /* "" = no icon cache */
    size_t cache_max;
    size_t cache_used; /* bytes in cache_dir, counted when pruning */
} s = {.events_fd = -1};

/* --- Events ---------------------------------------------------------------- */

static void emit(companion_evt_type_t type)
{
    companion_event_t e = {.type = type,
                           .online = s.online,
                           .needs_pairing = s.needs_pairing,
                           .field = s.field,
                           .field_id = s.field_id,
                           .version = s.version,
                           .can_update = s.can_update,
                           .update = s.update};
    strlcpy(e.version_name, s.version_name, sizeof(e.version_name));
    if (s.cb) {
        s.cb(&e, s.ctx);
    }
}

static void set_online(bool online)
{
    if (online == s.online) {
        return;
    }
    s.online = online;
    s.field = COMPANION_FIELD_NONE;
    if (!online) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        memset(&s.media, 0, sizeof(s.media));
        s.media_known = false;
        xSemaphoreGive(s.lock);
    }
    ESP_LOGI(TAG, "%s", online ? "online" : "offline");
    emit(COMPANION_EVT_ONLINE);
}

static void set_needs_pairing(bool needed)
{
    if (needed == s.needs_pairing) {
        return;
    }
    s.needs_pairing = needed;
    ESP_LOGI(TAG, "%s", needed ? "pairing needed" : "paired");
    emit(COMPANION_EVT_PAIRING);
}

/* --- The token from pairing ---------------------------------------------------- */

/* Tokens are hex (Pairing.java): anything else could add header lines. */
static bool token_ok(const char *t)
{
    size_t n = strlen(t);
    if (n == 0 || n > TOKEN_MAX) {
        return false;
    }
    for (; *t; t++) {
        if (!((*t >= '0' && *t <= '9') || (*t >= 'A' && *t <= 'F') || (*t >= 'a' && *t <= 'f'))) {
            return false;
        }
    }
    return true;
}

static void load_token(void)
{
    if (kv_get_str(NVS_NS, NVS_TOKEN, s.token, sizeof(s.token)) != ESP_OK || !token_ok(s.token)) {
        s.token[0] = '\0';
    }
}

static void save_token(const char *token)
{
    esp_err_t err = kv_set_str(NVS_NS, NVS_TOKEN, token);
    ESP_LOGI(TAG, "token saved: %s", esp_err_to_name(err));
}

/* --- HTTP over a plain socket ---------------------------------------------- */

static int tcp_open(const char *host, int io_timeout_ms)
{
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(PORT)};
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int r = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (r != 0 && errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = {.tv_sec = CONNECT_TIMEOUT_MS / 1000, .tv_usec = (CONNECT_TIMEOUT_MS % 1000) * 1000};
        int so_err = 0;
        socklen_t len = sizeof(so_err);
        r = (select(fd + 1, NULL, &wfds, NULL, &tv) == 1 &&
             getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len) == 0 && so_err == 0)
                ? 0
                : -1;
    }
    if (r != 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval tv = {.tv_sec = io_timeout_ms / 1000, .tv_usec = (io_timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

static bool send_request(int fd, const char *method, const char *path, const char *host)
{
    char token[sizeof(s.token)];
    xSemaphoreTake(s.lock, portMAX_DELAY);
    strlcpy(token, s.token, sizeof(token));
    xSemaphoreGive(s.lock);
    char auth[sizeof(token) + 32] = "";
    if (token[0]) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s\r\n", token);
    }
    char req[896]; /* the path may carry up to 128 percent-encoded bytes of text */
    int n = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: %s:%d\r\n%s%sConnection: close\r\n\r\n", method,
                     path, host, PORT, auth, strcmp(method, "POST") == 0 ? "Content-Length: 0\r\n" : "");
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        return false;
    }
    for (int sent = 0; sent < n;) {
        int r = send(fd, req + sent, n - sent, 0);
        if (r <= 0) {
            return false;
        }
        sent += r;
    }
    return true;
}

/* Reads the status line and headers (byte by byte: they are short, and the
 * body stays in the socket). Returns the status, or -1, also when they take
 * past REQUEST_MS or HEADER_BYTES_MAX (a peer trickling bytes). */
static int read_headers(int fd, long *content_length)
{
    char line[200];
    size_t n = 0;
    size_t total = 0;
    int status = -1;
    bool first = true;
    int64_t deadline = esp_timer_get_time() + REQUEST_MS * 1000LL;
    *content_length = -1;
    for (;;) {
        char ch;
        if (++total > HEADER_BYTES_MAX || esp_timer_get_time() > deadline || recv(fd, &ch, 1, 0) != 1) {
            return -1;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch != '\n') {
            if (n < sizeof(line) - 1) {
                line[n++] = ch;
            }
            continue;
        }
        line[n] = '\0';
        if (n == 0) {
            return status;
        }
        if (first) {
            first = false;
            if (sscanf(line, "HTTP/%*s %d", &status) != 1) {
                return -1;
            }
        } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
            *content_length = strtol(line + 15, NULL, 10);
        }
        n = 0;
    }
}

/* One request on its own connection. Returns the body of the answer, of at
 * most max bytes (PSRAM, NUL-terminated), or NULL; *status is the HTTP
 * status, or -1 if there was no answer. */
static uint8_t *http_status(const char *host, const char *method, const char *path, size_t max, size_t *len,
                            int *status)
{
    *status = -1;
    int fd = tcp_open(host, IO_TIMEOUT_MS);
    if (fd < 0) {
        return NULL;
    }
    uint8_t *body = NULL;
    long cl = -1;
    if (send_request(fd, method, path, host) && (*status = read_headers(fd, &cl)) > 0 && cl >= 0 &&
        (size_t)cl <= max) {
        body = heap_caps_malloc((size_t)cl + 1, MALLOC_CAP_SPIRAM);
        size_t got = 0;
        int64_t deadline = esp_timer_get_time() + REQUEST_MS * 1000LL;
        while (body && got < (size_t)cl && esp_timer_get_time() <= deadline) {
            int r = recv(fd, body + got, (size_t)cl - got, 0);
            if (r <= 0) {
                break;
            }
            got += (size_t)r;
        }
        if (body && got == (size_t)cl) {
            body[cl] = '\0';
            *len = (size_t)cl;
        } else {
            free(body);
            body = NULL;
        }
    }
    close(fd);
    return body;
}

/* The body of a 200 answer, or NULL (a 401 also marks pairing as needed). */
static uint8_t *http(const char *host, const char *method, const char *path, size_t max, size_t *len)
{
    int status;
    uint8_t *body = http_status(host, method, path, max, len, &status);
    if (status == 401) {
        set_needs_pairing(true);
    }
    if (status != 200) {
        free(body);
        body = NULL;
    }
    return body;
}

/* Package names are letters, digits, '.' and '_': nothing to URL-encode. */
static bool pkg_ok(const char *pkg)
{
    if (!pkg || !pkg[0] || strlen(pkg) >= COMPANION_PKG_MAX) {
        return false;
    }
    for (const char *p = pkg; *p; p++) {
        bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' ||
                  *p == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* --- App list ---------------------------------------------------------------- */

static uint8_t *fetch_icon(const char *host, const char *pkg, int size)
{
    char path[160];
    snprintf(path, sizeof(path), "/icon?pkg=%s&size=%d", pkg, size);
    size_t want = ICON_BYTES(size);
    size_t len = 0;
    uint8_t *icon = http(host, "GET", path, want, &len);
    if (icon && len != want) {
        free(icon);
        icon = NULL;
    }
    return icon;
}

static const companion_app_t *find_app(const char *pkg)
{
    for (size_t i = 0; i < s.count; i++) {
        if (strcmp(s.apps[i].pkg, pkg) == 0) {
            return &s.apps[i];
        }
    }
    return NULL;
}

static bool in_list(const companion_app_t *list, size_t n, const uint8_t *buf)
{
    for (size_t i = 0; i < n; i++) {
        if (list[i].icon == buf || list[i].icon_small == buf) {
            return true;
        }
    }
    return false;
}

static void retire(uint8_t *buf, uint32_t gen)
{
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < s.stale_count; i++) {
        if (s.stale[i].buf == buf) {
            return; /* already retired: freeing it twice would corrupt the heap */
        }
    }
    if (s.stale_count == s.stale_cap) {
        size_t cap = s.stale_cap ? s.stale_cap * 2 : 16;
        stale_t *grown = heap_caps_realloc(s.stale, cap * sizeof(stale_t), MALLOC_CAP_SPIRAM);
        if (!grown) {
            return; /* leak rather than free something the UI may draw */
        }
        s.stale = grown;
        s.stale_cap = cap;
    }
    s.stale[s.stale_count++] = (stale_t){.buf = buf, .gen = gen};
}

/* --- Icon cache ------------------------------------------------------------------ */

/* <cache_dir>/<FNV-1a of the package>.ico: a header, then both sizes
 * deflated (raw deflate, the ROM's miniz). The header repeats the package
 * and its "updated" value, so a hash collision or an updated app just
 * misses. The compressor's state (~150 KB) and the inflater's (~11 KB) live
 * in PSRAM while a refresh needs them. */
typedef struct {
    uint32_t magic;
    uint32_t len_big;   /* the big icon, deflated */
    uint32_t len_small; /* the small one, deflated */
    int64_t updated;
    char pkg[COMPANION_PKG_MAX];
} cache_head_t;

static tdefl_compressor *s_deflate;
static tinfl_decompressor *s_inflate;
static unsigned s_deflate_stack_left = DEFLATE_STACK; /* the least seen, for the log */

static void cache_path(char *out, size_t size, const char *pkg, const char *suffix)
{
    uint32_t h = 2166136261u;
    for (const char *p = pkg; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    snprintf(out, size, "%s/%08" PRIx32 "%s", s.cache_dir, h, suffix);
}

/* Deflates len bytes into out (cap bytes); 0 if it doesn't fit or fails. */
static size_t deflate_into(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    if (!s_deflate) {
        s_deflate = heap_caps_malloc(sizeof(*s_deflate), MALLOC_CAP_SPIRAM);
    }
    if (!s_deflate || tdefl_init(s_deflate, NULL, NULL, TDEFL_DEFAULT_MAX_PROBES) != TDEFL_STATUS_OKAY) {
        return 0;
    }
    size_t in_len = len;
    size_t out_len = cap;
    return tdefl_compress(s_deflate, in, &in_len, out, &out_len, TDEFL_FINISH) == TDEFL_STATUS_DONE ? out_len : 0;
}

/* Both icons of one app, deflated on the helper task (deflate_task). */
typedef struct {
    const uint8_t *big, *small;
    uint8_t *out; /* room for both, raw sizes */
    uint32_t len_big, len_small;
    SemaphoreHandle_t done;
} deflate_job_t;

static void deflate_task(void *arg)
{
    deflate_job_t *j = arg;
    j->len_big = deflate_into(j->big, ICON_BYTES(s.icon_px), j->out, ICON_BYTES(s.icon_px));
    j->len_small = j->len_big ? deflate_into(j->small, ICON_BYTES(s.icon_small_px), j->out + j->len_big,
                                             ICON_BYTES(s.icon_small_px))
                              : 0;
    unsigned left = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    s_deflate_stack_left = left < s_deflate_stack_left ? left : s_deflate_stack_left;
    xSemaphoreGive(j->done);
    vTaskDelete(NULL);
}

/* Deflates both icons on a short-lived task, so the companion task's own
 * stack can stay small. False if it couldn't. */
static bool deflate_icons(deflate_job_t *j)
{
    j->len_big = j->len_small = 0;
    j->done = xSemaphoreCreateBinary();
    if (!j->done) {
        return false;
    }
    if (xTaskCreate(deflate_task, "icon_zip", DEFLATE_STACK, j, uxTaskPriorityGet(NULL), NULL) == pdPASS) {
        xSemaphoreTake(j->done, portMAX_DELAY);
    }
    vSemaphoreDelete(j->done);
    return j->len_big > 0 && j->len_small > 0;
}

/* Inflates exactly want bytes into a new PSRAM buffer, or NULL. */
static uint8_t *inflate_new(const uint8_t *in, size_t len, size_t want)
{
    if (!s_inflate) {
        s_inflate = heap_caps_malloc(sizeof(*s_inflate), MALLOC_CAP_SPIRAM);
    }
    uint8_t *out = s_inflate ? heap_caps_malloc(want, MALLOC_CAP_SPIRAM) : NULL;
    if (!out) {
        return NULL;
    }
    tinfl_init(s_inflate);
    size_t in_len = len;
    size_t out_len = want;
    tinfl_status st =
        tinfl_decompress(s_inflate, in, &in_len, out, out, &out_len, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (st != TINFL_STATUS_DONE || out_len != want) {
        free(out);
        return NULL;
    }
    return out;
}

/* Both icons of pkg as cached for this "updated" value; false if missing. */
static bool cache_load(const char *pkg, int64_t updated, uint8_t **big, uint8_t **small)
{
    if (!s.cache_dir[0]) {
        return false;
    }
    char path[72];
    cache_path(path, sizeof(path), pkg, ".ico");
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    cache_head_t h;
    uint8_t *data = NULL;
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == CACHE_MAGIC && h.updated == updated &&
              strncmp(h.pkg, pkg, sizeof(h.pkg)) == 0 && h.len_big > 0 && h.len_small > 0 &&
              h.len_big <= ICON_BYTES(s.icon_px) && h.len_small <= ICON_BYTES(s.icon_small_px);
    if (ok) {
        data = heap_caps_malloc(h.len_big + h.len_small, MALLOC_CAP_SPIRAM);
        ok = data && fread(data, 1, h.len_big + h.len_small, f) == h.len_big + h.len_small;
    }
    fclose(f);
    *big = ok ? inflate_new(data, h.len_big, ICON_BYTES(s.icon_px)) : NULL;
    *small = *big ? inflate_new(data + h.len_big, h.len_small, ICON_BYTES(s.icon_small_px)) : NULL;
    free(data);
    if (!*small) {
        free(*big);
        *big = NULL;
        return false;
    }
    return true;
}

/* Saves both icons of pkg, unless the cache would grow past its limit. */
static void cache_save(const char *pkg, int64_t updated, const uint8_t *big, const uint8_t *small)
{
    if (!s.cache_dir[0]) {
        return;
    }
    size_t cap = ICON_BYTES(s.icon_px) + ICON_BYTES(s.icon_small_px);
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return;
    }
    cache_head_t h = {.magic = CACHE_MAGIC, .updated = updated};
    strlcpy(h.pkg, pkg, sizeof(h.pkg));
    deflate_job_t job = {.big = big, .small = small, .out = buf};
    if (deflate_icons(&job)) {
        h.len_big = job.len_big;
        h.len_small = job.len_small;
    }
    size_t total = sizeof(h) + h.len_big + h.len_small;
    if (h.len_small && s.cache_used + total <= s.cache_max) {
        /* Write aside and rename, so a cut-off write never looks like an icon. */
        char tmp[72], path[72];
        cache_path(tmp, sizeof(tmp), pkg, ".tmp");
        cache_path(path, sizeof(path), pkg, ".ico");
        FILE *f = fopen(tmp, "wb");
        bool ok = f && fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(buf, 1, h.len_big + h.len_small, f) == h.len_big + h.len_small;
        if (f && fclose(f) != 0) {
            ok = false;
        }
        remove(path);
        if (ok && rename(tmp, path) == 0) {
            s.cache_used += total;
        } else {
            remove(tmp);
        }
    }
    free(buf);
}

/* Drops files for apps no longer in the list (and leftovers), and counts
 * what stays. */
static void cache_prune(const companion_app_t *list, size_t n)
{
    DIR *d = s.cache_dir[0] ? opendir(s.cache_dir) : NULL;
    if (!d) {
        return;
    }
    s.cache_used = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        bool keep = false;
        for (size_t i = 0; i < n && !keep; i++) {
            char path[72];
            cache_path(path, sizeof(path), list[i].pkg, ".ico");
            keep = strcmp(strrchr(path, '/') + 1, e->d_name) == 0;
        }
        char path[48 + 2 + sizeof(e->d_name)];
        snprintf(path, sizeof(path), "%s/%s", s.cache_dir, e->d_name);
        struct stat st;
        if (!keep) {
            remove(path);
        } else if (stat(path, &st) == 0) {
            s.cache_used += (size_t)st.st_size;
        }
    }
    closedir(d);
}

static void cache_release(void)
{
    free(s_deflate);
    s_deflate = NULL;
    free(s_inflate);
    s_inflate = NULL;
}

/* --- App list (continued) ----------------------------------------------------------- */

static void refresh_apps(const char *host)
{
    size_t len = 0;
    char *json = (char *)http(host, "GET", "/apps", LIST_MAX_BYTES, &len);
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    free(json);
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "apps");
    if (!cJSON_IsArray(arr)) {
        ESP_LOGW(TAG, "app list: no answer");
        cJSON_Delete(root);
        return;
    }

    companion_app_t *next = heap_caps_calloc(COMPANION_MAX_APPS, sizeof(companion_app_t), MALLOC_CAP_SPIRAM);
    if (!next) {
        cJSON_Delete(root);
        return;
    }
    size_t count = 0;
    int fetched = 0;
    int cached = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        const cJSON *pkg = cJSON_GetObjectItemCaseSensitive(item, "pkg");
        const cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");
        const cJSON *updated = cJSON_GetObjectItemCaseSensitive(item, "updated");
        if (count == COMPANION_MAX_APPS || !cJSON_IsString(pkg) || !pkg_ok(pkg->valuestring)) {
            continue;
        }
        bool twice = false;
        for (size_t i = 0; i < count && !twice; i++) {
            twice = strcmp(next[i].pkg, pkg->valuestring) == 0;
        }
        if (twice) {
            continue; /* two entries would share (and later free) one icon */
        }
        companion_app_t *a = &next[count++];
        strlcpy(a->pkg, pkg->valuestring, sizeof(a->pkg));
        strlcpy(a->label, cJSON_IsString(label) ? label->valuestring : a->pkg, sizeof(a->label));
        a->updated = cJSON_IsNumber(updated) ? (int64_t)updated->valuedouble : 0;
        const companion_app_t *old = find_app(a->pkg);
        uint8_t *big = NULL, *small = NULL;
        if (old && old->updated == a->updated && old->icon && old->icon_small) {
            a->icon = old->icon;
            a->icon_small = old->icon_small;
        } else if (cache_load(a->pkg, a->updated, &big, &small)) {
            a->icon = big;
            a->icon_small = small;
            cached++;
        } else {
            a->icon = fetch_icon(host, a->pkg, s.icon_px);
            a->icon_small = fetch_icon(host, a->pkg, s.icon_small_px);
            if (a->icon && a->icon_small) {
                cache_save(a->pkg, a->updated, a->icon, a->icon_small);
            }
            fetched++;
        }
    }
    cJSON_Delete(root);
    cache_prune(next, count);
    cache_release();

    bool changed = fetched > 0 || cached > 0 || count != s.count;
    for (size_t i = 0; i < count && !changed; i++) {
        changed = strcmp(next[i].pkg, s.apps[i].pkg) != 0 || strcmp(next[i].label, s.apps[i].label) != 0;
    }
    if (!changed) {
        free(next);
        return;
    }

    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.gen++;
    for (size_t i = 0; i < s.count; i++) {
        uint8_t *bufs[2] = {(uint8_t *)s.apps[i].icon, (uint8_t *)s.apps[i].icon_small};
        for (int k = 0; k < 2; k++) {
            if (!in_list(next, count, bufs[k])) {
                retire(bufs[k], s.gen);
            }
        }
    }
    free(s.apps);
    s.apps = next;
    s.count = count;
    xSemaphoreGive(s.lock);
    ESP_LOGI(TAG, "%u apps, %d with new icons, %d from the cache (%u KB); stack left %u, deflate's %u",
             (unsigned)count, fetched, cached, (unsigned)(s.cache_used / 1024),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), s_deflate_stack_left);
    emit(COMPANION_EVT_APPS);
}

/* --- The task ------------------------------------------------------------------ */

/* {"pkg":..,"title":..,"subtitle":..,"state":..} from /media or an event. */
static void set_media(const cJSON *m)
{
    const cJSON *pkg = cJSON_GetObjectItemCaseSensitive(m, "pkg");
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(m, "title");
    const cJSON *subtitle = cJSON_GetObjectItemCaseSensitive(m, "subtitle");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(m, "state");
    const char *st = cJSON_IsString(state) ? state->valuestring : "none";
    companion_media_t now = {
        .state = strcmp(st, "playing") == 0     ? COMPANION_MEDIA_PLAYING
                 : strcmp(st, "paused") == 0    ? COMPANION_MEDIA_PAUSED
                 : strcmp(st, "buffering") == 0 ? COMPANION_MEDIA_BUFFERING
                 : strcmp(st, "stopped") == 0   ? COMPANION_MEDIA_STOPPED
                                                : COMPANION_MEDIA_NONE,
    };
    if (now.state != COMPANION_MEDIA_NONE && cJSON_IsString(pkg) && pkg_ok(pkg->valuestring)) {
        strlcpy(now.pkg, pkg->valuestring, sizeof(now.pkg));
        strlcpy(now.title, cJSON_IsString(title) ? title->valuestring : "", sizeof(now.title));
        strlcpy(now.subtitle, cJSON_IsString(subtitle) ? subtitle->valuestring : "", sizeof(now.subtitle));
    } else {
        now.state = COMPANION_MEDIA_NONE;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool changed = memcmp(&now, &s.media, sizeof(now)) != 0;
    s.media = now;
    xSemaphoreGive(s.lock);
    if (changed) {
        /* The app and state only: titles may be personal. */
        ESP_LOGI(TAG, "media: %s %s", now.pkg[0] ? now.pkg : "nothing", st);
        emit(COMPANION_EVT_MEDIA);
    }
}

/* The companion's version from /status (0.4 and older: no name, no updates). */
static void read_status(const char *host)
{
    size_t len = 0;
    char *json = (char *)http(host, "GET", "/status", 1024, &len);
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    free(json);
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    s.version = cJSON_IsNumber(version) ? version->valueint : 0;
    if (cJSON_IsString(name) && name->valuestring[0]) {
        strlcpy(s.version_name, name->valuestring, sizeof(s.version_name));
    } else if (s.version > 0) {
        /* The version code is MAJOR * 100 + MINOR (companion/build.ps1). */
        snprintf(s.version_name, sizeof(s.version_name), "%d.%d", s.version / 100, s.version % 100);
    } else {
        s.version_name[0] = '\0';
    }
    s.can_update = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "updates"));
    s.update = COMPANION_UPDATE_NONE;
    bool media = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "media"));
    cJSON_Delete(root);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.media_known = media;
    xSemaphoreGive(s.lock);
    if (media) {
        json = (char *)http(host, "GET", "/media", 1024, &len);
        root = json ? cJSON_Parse(json) : NULL;
        free(json);
        set_media(root);
        cJSON_Delete(root);
    }
    ESP_LOGI(TAG, "version %s%s", s.version_name[0] ? s.version_name : "unknown",
             s.can_update ? ", updates itself" : "");
}

static void set_speed(const cJSON *m)
{
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(m, "state");
    const char *st = cJSON_IsString(state) ? state->valuestring : "idle";
    const cJSON *progress = cJSON_GetObjectItemCaseSensitive(m, "progress");
    const cJSON *ping = cJSON_GetObjectItemCaseSensitive(m, "ping");
    const cJSON *down = cJSON_GetObjectItemCaseSensitive(m, "down");
    const cJSON *up = cJSON_GetObjectItemCaseSensitive(m, "up");
    companion_speed_t now = {
        .state = strcmp(st, "latency") == 0    ? COMPANION_SPEED_LATENCY
                 : strcmp(st, "download") == 0 ? COMPANION_SPEED_DOWNLOAD
                 : strcmp(st, "upload") == 0   ? COMPANION_SPEED_UPLOAD
                 : strcmp(st, "done") == 0     ? COMPANION_SPEED_DONE
                 : strcmp(st, "failed") == 0   ? COMPANION_SPEED_FAILED
                                               : COMPANION_SPEED_IDLE,
        .progress = cJSON_IsNumber(progress) ? progress->valueint : 0,
        .ping_ms = cJSON_IsNumber(ping) ? (float)ping->valuedouble : 0,
        .down_mbps = cJSON_IsNumber(down) ? (float)down->valuedouble : 0,
        .up_mbps = cJSON_IsNumber(up) ? (float)up->valuedouble : 0,
    };
    xSemaphoreTake(s.lock, portMAX_DELAY);
    companion_speed_state_t was = s.speed.state;
    s.speed = now;
    xSemaphoreGive(s.lock);
    if (now.state != was) {
        ESP_LOGI(TAG, "speed test: %s", st);
    }
    emit(COMPANION_EVT_SPEED);
}

static void handle_line(const char *line)
{
    if (!line[0]) {
        return; /* keep-alive */
    }
    cJSON *root = cJSON_Parse(line);
    const cJSON *update = cJSON_GetObjectItemCaseSensitive(root, "update");
    if (cJSON_IsString(update)) {
        const char *u = update->valuestring;
        s.update = strcmp(u, "downloading") == 0  ? COMPANION_UPDATE_DOWNLOADING
                   : strcmp(u, "installing") == 0 ? COMPANION_UPDATE_INSTALLING
                   : strcmp(u, "confirm") == 0    ? COMPANION_UPDATE_CONFIRM
                   : strcmp(u, "failed") == 0     ? COMPANION_UPDATE_FAILED
                                                  : COMPANION_UPDATE_NONE;
        ESP_LOGI(TAG, "update: %s", u);
        emit(COMPANION_EVT_UPDATE);
    }
    const cJSON *media = cJSON_GetObjectItemCaseSensitive(root, "media");
    if (cJSON_IsObject(media)) {
        set_media(media);
    }
    const cJSON *speed = cJSON_GetObjectItemCaseSensitive(root, "speed");
    if (cJSON_IsObject(speed)) {
        set_speed(speed);
    }
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(root, "field");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(f)) {
        companion_field_t field = strcmp(f->valuestring, "text") == 0       ? COMPANION_FIELD_TEXT
                                  : strcmp(f->valuestring, "password") == 0 ? COMPANION_FIELD_PASSWORD
                                  : strcmp(f->valuestring, "number") == 0   ? COMPANION_FIELD_NUMBER
                                                                            : COMPANION_FIELD_NONE;
        int field_id = cJSON_IsNumber(id) ? id->valueint : 0;
        if (field != s.field || field_id != s.field_id) {
            s.field = field;
            s.field_id = field_id;
            ESP_LOGI(TAG, "field: %s (%d)", f->valuestring, field_id);
            emit(COMPANION_EVT_FIELD);
        }
    }
    cJSON_Delete(root);
}

/* Reads events until the stream ends, goes quiet or the host changes. */
static void read_events(int fd, const char *host)
{
    static char line[1024]; /* this task only; media lines carry titles */
    size_t n = 0;
    int64_t refreshed = esp_timer_get_time();
    for (;;) {
        char buf[64];
        int r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) {
            return;
        }
        for (int i = 0; i < r; i++) {
            if (buf[i] == '\n') {
                line[n] = '\0';
                handle_line(line);
                n = 0;
            } else if (buf[i] != '\r' && n < sizeof(line) - 1) {
                line[n++] = buf[i];
            }
        }
        if (esp_timer_get_time() - refreshed > REFRESH_EVERY_US) {
            refreshed = esp_timer_get_time();
            refresh_apps(host);
        }
    }
}

static void task(void *arg)
{
    uint32_t backoff_ms = RETRY_MIN_MS;
    for (;;) {
        char host[sizeof(s.host)];
        xSemaphoreTake(s.lock, portMAX_DELAY);
        strlcpy(host, s.host, sizeof(host));
        xSemaphoreGive(s.lock);
        if (!host[0]) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        int fd = tcp_open(host, EVENTS_IDLE_MS);
        long cl;
        int status = -1;
        if (fd < 0 || !send_request(fd, "GET", "/events", host) || (status = read_headers(fd, &cl)) != 200) {
            if (fd >= 0) {
                close(fd);
            }
            set_online(false);
            if (status == 401) {
                /* Companion 0.4+ without a token, or one it forgot: wait for
                 * companion_pair() (it wakes the task), checking now and
                 * then in case it was reinstalled. */
                set_needs_pairing(true);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RETRY_MAX_MS));
                continue;
            }
            ESP_LOGD(TAG, "not reachable at %s:%d; retrying in %lu s", host, PORT, (unsigned long)(backoff_ms / 1000));
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff_ms)); /* a new host wakes it */
            backoff_ms = backoff_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : backoff_ms * 2;
            continue;
        }
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.events_fd = fd;
        xSemaphoreGive(s.lock);

        set_needs_pairing(false);
        int64_t opened = esp_timer_get_time();
        read_status(host);
        set_online(true);
        refresh_apps(host);
        read_events(fd, host);

        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.events_fd = -1;
        xSemaphoreGive(s.lock);
        close(fd);
        set_online(false);
        if (esp_timer_get_time() - opened < EVENTS_SHORT_US) {
            /* It ended at once: wait before fetching everything again. */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff_ms));
            backoff_ms = backoff_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : backoff_ms * 2;
        } else {
            backoff_ms = RETRY_MIN_MS;
        }
    }
}

/* --- API ------------------------------------------------------------------------ */

void companion_set_icon_sizes(int big, int small)
{
    s.icon_px = big;
    s.icon_small_px = small;
}

esp_err_t companion_start(companion_cb_t cb, void *ctx)
{
    if (!s.icon_px) {
        s.icon_px = COMPANION_ICON;
        s.icon_small_px = COMPANION_ICON_SMALL;
    }
    if (s.task) {
        return ESP_ERR_INVALID_STATE;
    }
    s.cb = cb;
    s.ctx = ctx;
    load_token();
    s.lock = xSemaphoreCreateMutex();
    if (!s.lock || xTaskCreate(task, "companion", TASK_STACK, NULL, 4, &s.task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void companion_set_cache_dir(const char *dir, size_t max_bytes)
{
    if (!dir || strlen(dir) >= sizeof(s.cache_dir)) {
        return;
    }
    struct stat st;
    if (stat(dir, &st) != 0 && mkdir(dir, 0775) != 0) {
        ESP_LOGW(TAG, "no icon cache: can't make %s", dir);
        return;
    }
    strlcpy(s.cache_dir, dir, sizeof(s.cache_dir));
    s.cache_max = max_bytes;
    s.cache_used = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        char path[48 + 2 + sizeof(e->d_name)];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (stat(path, &st) == 0) {
            s.cache_used += (size_t)st.st_size;
        }
    }
    if (d) {
        closedir(d);
    }
}

void companion_set_host(const char *host)
{
    if (!s.task) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool changed = strcmp(s.host, host ? host : "") != 0;
    if (changed) {
        strlcpy(s.host, host ? host : "", sizeof(s.host));
        if (s.events_fd >= 0) {
            shutdown(s.events_fd, SHUT_RDWR); /* ends read_events() */
        }
    }
    xSemaphoreGive(s.lock);
    if (changed) {
        xTaskNotifyGive(s.task);
    }
}

void companion_speed(companion_speed_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s.task) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    *out = s.speed;
    xSemaphoreGive(s.lock);
}

bool companion_media(companion_media_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s.task) {
        return false;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool known = s.media_known;
    *out = s.media;
    xSemaphoreGive(s.lock);
    return known;
}

size_t companion_apps(companion_app_t *out, size_t max, uint32_t *gen)
{
    if (!s.task) {
        *gen = 0;
        return 0;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    size_t n = s.count < max ? s.count : max;
    memcpy(out, s.apps, n * sizeof(companion_app_t));
    *gen = s.gen;
    xSemaphoreGive(s.lock);
    return n;
}

void companion_apps_applied(uint32_t gen)
{
    if (!s.task) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    size_t kept = 0;
    for (size_t i = 0; i < s.stale_count; i++) {
        if (s.stale[i].gen <= gen) {
            free(s.stale[i].buf);
        } else {
            s.stale[kept++] = s.stale[i];
        }
    }
    s.stale_count = kept;
    xSemaphoreGive(s.lock);
}

/* One short POST to path; ESP_OK on a 200 answer. */
static esp_err_t post(const char *path)
{
    if (!s.task) {
        return ESP_ERR_INVALID_STATE;
    }
    char host[sizeof(s.host)];
    xSemaphoreTake(s.lock, portMAX_DELAY);
    strlcpy(host, s.host, sizeof(host));
    bool online = s.online;
    xSemaphoreGive(s.lock);
    if (!online) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t len = 0;
    uint8_t *body = http(host, "POST", path, 256, &len);
    bool ok = body != NULL;
    free(body);
    return ok ? ESP_OK : ESP_FAIL;
}

/* path = prefix + text, percent-encoded (at most 128 bytes of text). Unlike
 * hub_url_encode(), it keeps what fits instead of failing: a longer text is
 * typed in part. */
static void encode_into(char *path, size_t size, const char *text)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t n = strlen(path);
    for (const unsigned char *p = (const unsigned char *)text; *p && n + 4 < size; p++) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                     *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (plain) {
            path[n++] = (char)*p;
        } else {
            path[n++] = '%';
            path[n++] = HEX[*p >> 4];
            path[n++] = HEX[*p & 15];
        }
    }
    path[n] = '\0';
}

esp_err_t companion_set_text(const char *text)
{
    char path[16 + 128 * 3 + 1] = "/text?value=";
    encode_into(path, sizeof(path), text);
    return post(path);
}

esp_err_t companion_search(const char *text)
{
    char path[16 + 128 * 3 + 1] = "/search?q=";
    encode_into(path, sizeof(path), text);
    return post(path);
}

esp_err_t companion_enter(void)
{
    return post("/enter");
}

esp_err_t companion_speedtest(void)
{
    return post("/speedtest");
}

esp_err_t companion_notify(const char *text, int seconds)
{
    char path[40 + 160 * 3 + 1];
    snprintf(path, sizeof(path), "/notify?seconds=%d&text=", seconds);
    encode_into(path, sizeof(path), text);
    return post(path);
}

esp_err_t companion_hide_keyboard(bool hidden)
{
    return post(hidden ? "/keyboard?hidden=1" : "/keyboard?hidden=0");
}

esp_err_t companion_update(const char *url, int size, const char *sha256)
{
    if (!url || strlen(url) > 128 || size <= 0 || !sha256 || strlen(sha256) != 64) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[16 + 128 * 3 + 96] = "/update?url=";
    encode_into(path, sizeof(path), url);
    size_t n = strlen(path);
    snprintf(path + n, sizeof(path) - n, "&size=%d&sha256=%s", size, sha256);
    return post(path);
}

/* A pairing request: the companion is offline while it needs pairing, so
 * this only needs a host. */
static uint8_t *pair_request(const char *path, size_t *len, int *status)
{
    char host[sizeof(s.host)];
    xSemaphoreTake(s.lock, portMAX_DELAY);
    strlcpy(host, s.host, sizeof(host));
    xSemaphoreGive(s.lock);
    if (!host[0]) {
        *status = -1;
        return NULL;
    }
    return http_status(host, "POST", path, 256, len, status);
}

esp_err_t companion_pair_start(uint32_t *locked_s)
{
    if (locked_s) {
        *locked_s = 0;
    }
    if (!s.task) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t len = 0;
    int status;
    char *body = (char *)pair_request("/pair/start", &len, &status);
    ESP_LOGI(TAG, "pairing code requested: %d", status);
    esp_err_t err = ESP_FAIL;
    if (status == 200) {
        err = ESP_OK;
    } else if (status == 429) {
        err = ESP_ERR_NOT_FINISHED; /* no code showing: the last went up under 5 s ago */
    } else if (status == 423) {
        err = ESP_ERR_NOT_ALLOWED; /* too many wrong codes: {"retry":seconds} */
        cJSON *root = body ? cJSON_Parse(body) : NULL;
        const cJSON *retry = cJSON_GetObjectItemCaseSensitive(root, "retry");
        if (locked_s && cJSON_IsNumber(retry) && retry->valuedouble > 0) {
            *locked_s = retry->valuedouble < 86400 ? (uint32_t)retry->valuedouble : 86400;
        }
        cJSON_Delete(root);
    }
    free(body);
    return err;
}

esp_err_t companion_pair(const char *code)
{
    if (!s.task || !code || !code[0] || strlen(code) > 16) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[48] = "/pair?code=";
    size_t n = strlen(path);
    for (const char *p = code; *p; p++) {
        bool ok = (*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f');
        if (!ok) {
            return ESP_ERR_INVALID_ARG;
        }
        path[n++] = *p;
    }
    path[n] = '\0';

    size_t len = 0;
    int status;
    char *body = (char *)pair_request(path, &len, &status);
    esp_err_t err = ESP_FAIL;
    if (status == 200) {
        cJSON *root = body ? cJSON_Parse(body) : NULL;
        const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
        if (cJSON_IsString(token) && token_ok(token->valuestring)) {
            xSemaphoreTake(s.lock, portMAX_DELAY);
            strlcpy(s.token, token->valuestring, sizeof(s.token));
            xSemaphoreGive(s.lock);
            save_token(token->valuestring);
            err = ESP_OK;
        }
        cJSON_Delete(root);
    } else if (status == 403) {
        err = ESP_ERR_INVALID_ARG; /* wrong code; after 3 wrong ones it is gone (410) */
    } else if (status == 410) {
        err = ESP_ERR_TIMEOUT; /* no code on the TV any more */
    }
    free(body);
    ESP_LOGI(TAG, "pairing: %d, %s", status, esp_err_to_name(err));
    if (err == ESP_OK) {
        xTaskNotifyGive(s.task); /* connect now, with the token */
    }
    return err;
}

void companion_pair_cancel(void)
{
    if (!s.task) {
        return;
    }
    size_t len = 0;
    int status;
    free(pair_request("/pair/cancel", &len, &status));
}

esp_err_t companion_launch(const char *pkg)
{
    if (!pkg_ok(pkg)) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[128];
    snprintf(path, sizeof(path), "/launch?pkg=%s", pkg);
    return post(path);
}
