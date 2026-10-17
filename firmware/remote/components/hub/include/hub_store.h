#pragma once

/*
 * Per-module storage.
 *  - Small settings: NVS, in the namespace "m_<module id>".
 *  - Files: /storage/<module id>/ on the LittleFS partition (2 MB, shared).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hub_kv_get_str(const char *module, const char *key, char *out, size_t len);
esp_err_t hub_kv_set_str(const char *module, const char *key, const char *value);
esp_err_t hub_kv_get_i32(const char *module, const char *key, int32_t *out);
esp_err_t hub_kv_set_i32(const char *module, const char *key, int32_t value);

/* Writes "/storage/<module>/<name>" to out and creates the folder. False
 * when the storage partition isn't mounted or out is too small. */
bool hub_file_path(const char *module, const char *name, char *out, size_t len);
/* Whole-file helpers. hub_file_read allocates *out (PSRAM, NUL-terminated);
 * free() it. */
esp_err_t hub_file_write(const char *path, const void *data, size_t len);
esp_err_t hub_file_read(const char *path, char **out, size_t *len, size_t max_len);

#ifdef __cplusplus
}
#endif
