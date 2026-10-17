#pragma once

/*
 * On-device self-test of the atv_remote wire layer. Needs no network and no
 * TV: it checks the hand-written protobuf encoder, the varint framing, the
 * message parsers and the pairing-secret hash against golden vectors that
 * were produced with the official python protobuf runtime (see
 * src/atv_golden.h for how they were generated).
 *
 * Can be called before or without atv_init(). Needs about 3 KB of stack
 * (call it from a task with at least 4 KB) plus ~1.7 KB of heap. Logs every
 * mismatch with a hex dump (tag "atv_test").
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ESP_OK when every check passes, ESP_FAIL otherwise. */
esp_err_t atv_selftest(void);

#ifdef __cplusplus
}
#endif
