#pragma once

/*
 * Small NVS helpers: each call opens a namespace, reads or writes one value
 * (and commits), and closes it. For settings read at start and written when
 * the user changes them; not for hot paths.
 *
 *  - Getters return ESP_ERR_NVS_NOT_FOUND when the namespace or key isn't
 *    there. kv_get_str() leaves out empty on any failure; the others leave
 *    *out unchanged, so it can hold the default.
 *  - Setters and kv_erase() log a warning when the write fails, so a full
 *    or damaged NVS shows in the log. Erasing a missing key is not a
 *    failure.
 */

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t kv_get_str(const char *ns, const char *key, char *out, size_t len);
esp_err_t kv_set_str(const char *ns, const char *key, const char *value);
esp_err_t kv_get_i32(const char *ns, const char *key, int32_t *out);
esp_err_t kv_set_i32(const char *ns, const char *key, int32_t value);
esp_err_t kv_get_u8(const char *ns, const char *key, uint8_t *out);
esp_err_t kv_set_u8(const char *ns, const char *key, uint8_t value);
esp_err_t kv_erase(const char *ns, const char *key);

#ifdef __cplusplus
}
#endif
