/* Small NVS helpers (kv.h). */
#include "kv.h"

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "kv";

static esp_err_t saved(const char *ns, const char *key, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s/%s not saved: %s", ns, key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t kv_get_str(const char *ns, const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    size_t n = len; /* nvs_get_str() may change it, even when it fails */
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_str(h, key, out, &n);
        nvs_close(h);
    }
    if (err != ESP_OK && len > 0) {
        out[0] = '\0';
    }
    return err;
}

esp_err_t kv_set_str(const char *ns, const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return saved(ns, key, err);
}

esp_err_t kv_get_i32(const char *ns, const char *key, int32_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err == ESP_OK) {
        int32_t v;
        err = nvs_get_i32(h, key, &v);
        if (err == ESP_OK) {
            *out = v;
        }
        nvs_close(h);
    }
    return err;
}

esp_err_t kv_set_i32(const char *ns, const char *key, int32_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return saved(ns, key, err);
}

esp_err_t kv_get_u8(const char *ns, const char *key, uint8_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err == ESP_OK) {
        uint8_t v;
        err = nvs_get_u8(h, key, &v);
        if (err == ESP_OK) {
            *out = v;
        }
        nvs_close(h);
    }
    return err;
}

esp_err_t kv_set_u8(const char *ns, const char *key, uint8_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return saved(ns, key, err);
}

esp_err_t kv_erase(const char *ns, const char *key)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_key(h, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return saved(ns, key, err);
}
