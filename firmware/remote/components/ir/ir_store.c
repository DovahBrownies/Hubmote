/*
 * Learned remotes as files (contract in include/ir_store.h): each remote is
 * <dir>/<id>.json, in the folder ir_store_init() was given (the app's
 * LittleFS, /storage/ir). File format (version 1):
 *
 *   {
 *     "version": 1,
 *     "id": "samsung_tv",
 *     "name": "Samsung TV",
 *     "buttons": [
 *       { "label": "Power", "slot": 0, "code": "SAMSUNG32 E0E040BF" }
 *     ]
 *   }
 *
 * Saves write <id>.tmp and rename it over <id>.json, so a power cut leaves
 * either the old file or the new one. Loads are lenient (bad buttons are
 * skipped with a warning, the file name decides the id); saves are strict.
 */

#include "ir_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "ir_store";

static char s_dir[24]; /* where the remotes are; with "/<id>.json" a path fits IR_STORE_PATH_MAX */

#define IR_STORE_DIR       s_dir
#define IR_STORE_EXT       ".json"
#define IR_STORE_TMP_EXT   ".tmp"
#define IR_STORE_VERSION   1
#define IR_STORE_ID_LEN    (sizeof(((ir_remote_t *)0)->id) - 1) /* 15 */
#define IR_STORE_FILE_MAX  (160 * 1024) /* 24 worst-case RAW buttons fit */
#define IR_STORE_PATH_MAX  48 /* the folder, "/", the id and ".json" */

_Static_assert(IR_STORE_MAX_BUTTONS <= 32, "slot bitmask is 32 bits");

static bool s_ready;
static ir_code_t *s_code; /* scratch for validating codes on save (PSRAM) */

static void *ir_store_alloc(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_DEFAULT);
}

/* --- Helpers ----------------------------------------------------------------- */

static bool id_char_ok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

static bool id_valid(const char *id)
{
    if (!id) {
        return false;
    }
    size_t n = strnlen(id, IR_STORE_ID_LEN + 1);
    if (n == 0 || n > IR_STORE_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!id_char_ok(id[i])) {
            return false;
        }
    }
    return true;
}

static void id_path(char *path, const char *id, const char *ext)
{
    snprintf(path, IR_STORE_PATH_MAX, "%s/%s%s", IR_STORE_DIR, id, ext);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Directory entry "<id>.json" -> id. */
static bool id_from_filename(const char *name, char id[IR_STORE_ID_LEN + 1])
{
    size_t n = strlen(name);
    size_t ext = strlen(IR_STORE_EXT);
    if (n <= ext || strcmp(name + n - ext, IR_STORE_EXT) != 0 || n - ext > IR_STORE_ID_LEN) {
        return false;
    }
    memcpy(id, name, n - ext);
    id[n - ext] = '\0';
    return id_valid(id);
}

static bool name_ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

/* Copies src, truncating at a UTF-8 character boundary if it doesn't fit.
 * Returns false if it was truncated. */
static bool copy_text(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    bool fits = n < size;
    if (!fits) {
        n = size - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--; /* src[n] continues a character: drop that character */
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return fits;
}

static int ascii_casecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a;
        int cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }
        if (ca != cb || ca == 0) {
            return ca - cb;
        }
    }
}

static esp_err_t read_file(const char *path, char **out)
{
    *out = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    esp_err_t err = ESP_OK;
    char *buf = NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        size = ftell(f);
    }
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        err = ESP_FAIL;
    } else if (size > IR_STORE_FILE_MAX) {
        ESP_LOGW(TAG, "%s: %ld bytes, larger than any valid remote", path, size);
        err = ESP_ERR_INVALID_SIZE;
    } else if (!(buf = ir_store_alloc((size_t)size + 1))) {
        err = ESP_ERR_NO_MEM;
    } else if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        err = ESP_FAIL;
    }
    fclose(f);
    if (err != ESP_OK) {
        heap_caps_free(buf);
        return err;
    }
    buf[size] = '\0';
    *out = buf;
    return ESP_OK;
}

static esp_err_t load_json(const char *id, cJSON **root)
{
    char path[IR_STORE_PATH_MAX];
    id_path(path, id, IR_STORE_EXT);
    char *text = NULL;
    esp_err_t err = read_file(path, &text);
    if (err != ESP_OK) {
        return err;
    }
    *root = cJSON_Parse(text);
    heap_caps_free(text);
    if (!*root || !cJSON_IsObject(*root)) {
        cJSON_Delete(*root);
        *root = NULL;
        ESP_LOGW(TAG, "%s: not a JSON object", path);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

/* Walks the "buttons" array, skipping malformed entries, slots out of
 * range, repeated slots and anything past IR_STORE_MAX_BUTTONS. Fills out
 * (if given) and returns the number of usable buttons. */
static uint8_t walk_buttons(const cJSON *root, const char *id, ir_remote_t *out)
{
    const cJSON *buttons = cJSON_GetObjectItemCaseSensitive(root, "buttons");
    if (!cJSON_IsArray(buttons)) {
        if (buttons) {
            ESP_LOGW(TAG, "%s: \"buttons\" is not an array", id);
        }
        return 0;
    }
    uint32_t used = 0;
    uint8_t n = 0;
    int idx = -1;
    const cJSON *b;
    cJSON_ArrayForEach(b, buttons) {
        idx++;
        const cJSON *label = cJSON_GetObjectItemCaseSensitive(b, "label");
        const cJSON *slot = cJSON_GetObjectItemCaseSensitive(b, "slot");
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(b, "code");
        if (!cJSON_IsString(label) || !cJSON_IsNumber(slot) || !cJSON_IsString(code)) {
            ESP_LOGW(TAG, "%s: button %d malformed, skipped", id, idx);
            continue;
        }
        double s = slot->valuedouble;
        if (s < 0 || s >= IR_STORE_MAX_BUTTONS || s != (double)(int)s) {
            ESP_LOGW(TAG, "%s: button %d slot out of range, skipped", id, idx);
            continue;
        }
        if (used & (1u << (int)s)) {
            ESP_LOGW(TAG, "%s: button %d repeats slot %d, skipped", id, idx, (int)s);
            continue;
        }
        if (strlen(code->valuestring) >= IR_CODE_TEXT_MAX) {
            ESP_LOGW(TAG, "%s: button %d code too long, skipped", id, idx);
            continue;
        }
        if (n == IR_STORE_MAX_BUTTONS) {
            ESP_LOGW(TAG, "%s: more than %d buttons, rest skipped", id, IR_STORE_MAX_BUTTONS);
            break;
        }
        used |= 1u << (int)s;
        if (out) {
            ir_button_t *dst = &out->buttons[n];
            if (!copy_text(dst->label, sizeof(dst->label), label->valuestring)) {
                ESP_LOGW(TAG, "%s: button %d label truncated", id, idx);
            }
            dst->slot = (uint8_t)s;
            copy_text(dst->code, sizeof(dst->code), code->valuestring);
        }
        n++;
    }
    return n;
}

static void read_name(const cJSON *root, const char *id, char *name, size_t size)
{
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (cJSON_IsString(j) && j->valuestring[0]) {
        if (!copy_text(name, size, j->valuestring)) {
            ESP_LOGW(TAG, "%s: name truncated", id);
        }
    } else {
        ESP_LOGW(TAG, "%s: no name, using the id", id);
        copy_text(name, size, id);
    }
}

static size_t count_remotes(void)
{
    size_t n = 0;
    DIR *d = opendir(IR_STORE_DIR);
    if (!d) {
        return 0;
    }
    char id[IR_STORE_ID_LEN + 1];
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (id_from_filename(e->d_name, id)) {
            n++;
        }
    }
    closedir(d);
    return n;
}

/* [a-z0-9_] from the name: letters lowercased, runs of anything else become
 * one '_', no leading or trailing '_', at most 15 characters, "remote" if
 * nothing is left; then _2, _3 ... until it's not taken. */
static esp_err_t derive_id(const char *name, char id[IR_STORE_ID_LEN + 1])
{
    char base[IR_STORE_ID_LEN + 1];
    size_t n = 0;
    bool sep = false;
    for (const char *p = name; *p && n < IR_STORE_ID_LEN; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            base[n++] = c;
            sep = false;
        } else if (n > 0 && !sep) {
            base[n++] = '_';
            sep = true;
        }
    }
    while (n > 0 && base[n - 1] == '_') {
        n--;
    }
    base[n] = '\0';
    if (n == 0) {
        strcpy(base, "remote");
    }

    char path[IR_STORE_PATH_MAX];
    for (int k = 1; k < 100; k++) {
        if (k == 1) {
            strcpy(id, base);
        } else {
            char suffix[8];
            snprintf(suffix, sizeof(suffix), "_%d", k);
            size_t keep = strlen(base);
            if (keep + strlen(suffix) > IR_STORE_ID_LEN) {
                keep = IR_STORE_ID_LEN - strlen(suffix);
            }
            memcpy(id, base, keep);
            strcpy(id + keep, suffix);
        }
        id_path(path, id, IR_STORE_EXT);
        if (!file_exists(path)) {
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t write_atomic(const char *id, const char *text)
{
    char path[IR_STORE_PATH_MAX];
    char tmp[IR_STORE_PATH_MAX];
    id_path(path, id, IR_STORE_EXT);
    id_path(tmp, id, IR_STORE_TMP_EXT);

    FILE *f = fopen(tmp, "wb");
    ESP_RETURN_ON_FALSE(f, ESP_FAIL, TAG, "can't create %s (errno %d)", tmp, errno);
    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len;
    ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        unlink(tmp);
        ESP_LOGE(TAG, "writing %s failed (errno %d)", tmp, errno);
        return ESP_FAIL;
    }
    if (rename(tmp, path) != 0) {
        int e = errno;
        unlink(tmp);
        ESP_LOGE(TAG, "rename to %s failed (errno %d)", path, e);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* --- API ------------------------------------------------------------------------- */

esp_err_t ir_store_init(const char *dir)
{
    if (s_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(dir && dir[0] && strlen(dir) < sizeof(s_dir), ESP_ERR_INVALID_ARG, TAG, "bad folder");
    strlcpy(s_dir, dir, sizeof(s_dir));
    struct stat st;
    if (stat(IR_STORE_DIR, &st) != 0) {
        ESP_RETURN_ON_FALSE(mkdir(IR_STORE_DIR, 0775) == 0, ESP_FAIL, TAG, "mkdir %s (errno %d)",
                            IR_STORE_DIR, errno);
    } else {
        ESP_RETURN_ON_FALSE(S_ISDIR(st.st_mode), ESP_FAIL, TAG, "%s is not a directory",
                            IR_STORE_DIR);
    }

    /* Leftovers from a save that lost power before its rename */
    DIR *d = opendir(IR_STORE_DIR);
    ESP_RETURN_ON_FALSE(d, ESP_FAIL, TAG, "opendir %s", IR_STORE_DIR);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (name_ends_with(e->d_name, IR_STORE_TMP_EXT)) {
            char path[IR_STORE_PATH_MAX + 32];
            snprintf(path, sizeof(path), "%s/%s", IR_STORE_DIR, e->d_name);
            ESP_LOGW(TAG, "removing unfinished save %s", path);
            unlink(path);
        }
    }
    closedir(d);

    if (!s_code) {
        s_code = ir_store_alloc(sizeof(*s_code));
        ESP_RETURN_ON_FALSE(s_code, ESP_ERR_NO_MEM, TAG, "scratch code");
    }
    ESP_LOGI(TAG, "%s: %u remotes", IR_STORE_DIR, (unsigned)count_remotes());
    s_ready = true;
    return ESP_OK;
}

static int info_cmp(const void *a, const void *b)
{
    const ir_remote_info_t *x = a;
    const ir_remote_info_t *y = b;
    int c = ascii_casecmp(x->name, y->name);
    return c ? c : strcmp(x->id, y->id);
}

esp_err_t ir_store_list(ir_remote_info_t *out, size_t max, size_t *count)
{
    ESP_RETURN_ON_FALSE(count && (out || max == 0), ESP_ERR_INVALID_ARG, TAG, "bad arguments");
    *count = 0;
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "ir_store_init not called");

    DIR *d = opendir(IR_STORE_DIR);
    ESP_RETURN_ON_FALSE(d, ESP_FAIL, TAG, "opendir %s", IR_STORE_DIR);
    size_t n = 0;
    char id[IR_STORE_ID_LEN + 1];
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!id_from_filename(e->d_name, id)) {
            continue;
        }
        if (n == max) {
            ESP_LOGW(TAG, "list: more remotes than the %u asked for", (unsigned)max);
            break;
        }
        cJSON *root = NULL;
        if (load_json(id, &root) != ESP_OK) {
            ESP_LOGW(TAG, "list: %s unreadable, skipped", id);
            continue;
        }
        ir_remote_info_t *info = &out[n];
        memset(info, 0, sizeof(*info));
        strcpy(info->id, id);
        read_name(root, id, info->name, sizeof(info->name));
        info->button_count = walk_buttons(root, id, NULL);
        cJSON_Delete(root);
        n++;
    }
    closedir(d);
    qsort(out, n, sizeof(*out), info_cmp);
    *count = n;
    return ESP_OK;
}

esp_err_t ir_store_load(const char *id, ir_remote_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "no output");
    ESP_RETURN_ON_FALSE(id_valid(id), ESP_ERR_INVALID_ARG, TAG, "bad id");
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "ir_store_init not called");

    cJSON *root = NULL;
    esp_err_t err = load_json(id, &root);
    if (err != ESP_OK) {
        return err;
    }
    memset(out, 0, sizeof(*out));
    strcpy(out->id, id);
    const cJSON *file_id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(file_id) && strcmp(file_id->valuestring, id) != 0) {
        ESP_LOGW(TAG, "%s: file says id \"%s\", using the file name", id, file_id->valuestring);
    }
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (cJSON_IsNumber(ver) && ver->valuedouble > IR_STORE_VERSION) {
        ESP_LOGW(TAG, "%s: written by a newer firmware (version %d)", id, (int)ver->valuedouble);
    }
    read_name(root, id, out->name, sizeof(out->name));
    out->button_count = walk_buttons(root, id, out);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t check_remote(const ir_remote_t *r)
{
    ESP_RETURN_ON_FALSE(memchr(r->name, '\0', sizeof(r->name)) && r->name[0], ESP_ERR_INVALID_ARG,
                        TAG, "name empty or not terminated");
    ESP_RETURN_ON_FALSE(memchr(r->id, '\0', sizeof(r->id)), ESP_ERR_INVALID_ARG, TAG,
                        "id not terminated");
    ESP_RETURN_ON_FALSE(r->id[0] == '\0' || id_valid(r->id), ESP_ERR_INVALID_ARG, TAG,
                        "id \"%s\" must be 1..15 of [a-z0-9_]", r->id);
    ESP_RETURN_ON_FALSE(r->button_count <= IR_STORE_MAX_BUTTONS, ESP_ERR_INVALID_ARG, TAG,
                        "%u buttons, max %d", r->button_count, IR_STORE_MAX_BUTTONS);
    uint32_t used = 0;
    for (int i = 0; i < r->button_count; i++) {
        const ir_button_t *b = &r->buttons[i];
        ESP_RETURN_ON_FALSE(memchr(b->label, '\0', sizeof(b->label)), ESP_ERR_INVALID_ARG, TAG,
                            "button %d label not terminated", i);
        ESP_RETURN_ON_FALSE(b->slot < IR_STORE_MAX_BUTTONS, ESP_ERR_INVALID_ARG, TAG,
                            "button %d slot %u out of range", i, b->slot);
        ESP_RETURN_ON_FALSE(!(used & (1u << b->slot)), ESP_ERR_INVALID_ARG, TAG,
                            "button %d repeats slot %u", i, b->slot);
        used |= 1u << b->slot;
        ESP_RETURN_ON_FALSE(memchr(b->code, '\0', sizeof(b->code)), ESP_ERR_INVALID_ARG, TAG,
                            "button %d code not terminated", i);
        /* An empty code is a button that hasn't been learned yet. */
        if (b->code[0]) {
            ESP_RETURN_ON_ERROR(ir_code_from_text(b->code, s_code), TAG, "button %d code", i);
        }
    }
    return ESP_OK;
}

static char *remote_to_json(const ir_remote_t *r, const char *id)
{
    char *text = NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON *buttons = NULL;
    bool ok = root && cJSON_AddNumberToObject(root, "version", IR_STORE_VERSION) &&
              cJSON_AddStringToObject(root, "id", id) &&
              cJSON_AddStringToObject(root, "name", r->name) &&
              (buttons = cJSON_AddArrayToObject(root, "buttons")) != NULL;
    for (int i = 0; ok && i < r->button_count; i++) {
        const ir_button_t *b = &r->buttons[i];
        cJSON *o = cJSON_CreateObject();
        ok = o && cJSON_AddStringToObject(o, "label", b->label) &&
             cJSON_AddNumberToObject(o, "slot", b->slot) &&
             cJSON_AddStringToObject(o, "code", b->code);
        if (o && !ok) {
            cJSON_Delete(o);
        } else if (ok) {
            ok = cJSON_AddItemToArray(buttons, o);
        }
    }
    if (ok) {
        text = cJSON_Print(root);
    }
    cJSON_Delete(root);
    return text;
}

esp_err_t ir_store_save(ir_remote_t *r)
{
    ESP_RETURN_ON_FALSE(r, ESP_ERR_INVALID_ARG, TAG, "no remote");
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "ir_store_init not called");
    ESP_RETURN_ON_ERROR(check_remote(r), TAG, "save rejected");

    char id[IR_STORE_ID_LEN + 1];
    if (r->id[0]) {
        strcpy(id, r->id);
    } else {
        ESP_RETURN_ON_ERROR(derive_id(r->name, id), TAG, "no free id for \"%s\"", r->name);
    }
    char path[IR_STORE_PATH_MAX];
    id_path(path, id, IR_STORE_EXT);
    if (!file_exists(path)) {
        ESP_RETURN_ON_FALSE(count_remotes() < IR_STORE_MAX_REMOTES, ESP_ERR_NO_MEM, TAG,
                            "store full (%d remotes)", IR_STORE_MAX_REMOTES);
    }

    char *text = remote_to_json(r, id);
    ESP_RETURN_ON_FALSE(text, ESP_ERR_NO_MEM, TAG, "JSON for %s", id);
    esp_err_t err = write_atomic(id, text);
    cJSON_free(text);
    if (err == ESP_OK) {
        strcpy(r->id, id);
        ESP_LOGI(TAG, "saved %s (\"%s\", %u buttons)", id, r->name, r->button_count);
    }
    return err;
}

esp_err_t ir_store_delete(const char *id)
{
    ESP_RETURN_ON_FALSE(id_valid(id), ESP_ERR_INVALID_ARG, TAG, "bad id");
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "ir_store_init not called");
    char path[IR_STORE_PATH_MAX];
    id_path(path, id, IR_STORE_EXT);
    if (unlink(path) != 0) {
        if (errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "delete %s failed (errno %d)", path, errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "deleted %s", id);
    return ESP_OK;
}
