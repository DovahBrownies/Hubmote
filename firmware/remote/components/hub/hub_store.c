/* Per-module NVS settings and files on the storage partition. */
#include "hub_store.h"

#include <stdio.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "nvs.h"

#define MOUNT "/storage" /* the app's LittleFS (main/storage.c, STORAGE_MOUNT) */

static void ns_of(const char *module, char out[16])
{
    snprintf(out, 16, "m_%s", module);
}

static esp_err_t open_ns(const char *module, nvs_open_mode_t mode, nvs_handle_t *h)
{
    char ns[16];
    ns_of(module, ns);
    return nvs_open(ns, mode, h);
}

esp_err_t hub_kv_get_str(const char *module, const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(module, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_str(h, key, out, &len);
        nvs_close(h);
    }
    return err;
}

esp_err_t hub_kv_set_str(const char *module, const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(module, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

esp_err_t hub_kv_get_i32(const char *module, const char *key, int32_t *out)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(module, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_i32(h, key, out);
        nvs_close(h);
    }
    return err;
}

esp_err_t hub_kv_set_i32(const char *module, const char *key, int32_t value)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(module, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

bool hub_file_path(const char *module, const char *name, char *out, size_t len)
{
    struct stat st;
    if (stat(MOUNT, &st) != 0) {
        return false;
    }
    int n = snprintf(out, len, MOUNT "/%s", module);
    if (n < 0 || (size_t)n >= len) {
        return false;
    }
    if (stat(out, &st) != 0 && mkdir(out, 0775) != 0) {
        return false;
    }
    n = snprintf(out, len, MOUNT "/%s/%s", module, name);
    return n > 0 && (size_t)n < len;
}

esp_err_t hub_file_write(const char *path, const void *data, size_t len)
{
    char tmp[96];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return ESP_FAIL;
    }
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (ok && rename(tmp, path) != 0) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) {
        remove(tmp);
    }
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t hub_file_read(const char *path, char **out, size_t *len, size_t max_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || (size_t)size > max_len) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    char *buf = heap_caps_malloc((size_t)size + 1, MALLOC_CAP_SPIRAM);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    if (len) {
        *len = got;
    }
    return got == (size_t)size ? ESP_OK : ESP_FAIL;
}
