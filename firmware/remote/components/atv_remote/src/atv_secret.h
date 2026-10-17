#pragma once

/*
 * Pairing secret of pairing.py (pure computation: SHA-256 + bignum only, no
 * NVS / X.509, so the self-test can exercise it in isolation).
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mbedtls/bignum.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATV_SECRET_LEN 32

/* True for exactly 6 hex digits (either case). */
bool atv_code_valid(const char *code);

/* pairing.py secret: SHA-256 over client n, client e, server n, server e
 * (each as minimal big-endian bytes, which is what bytes.fromhex(f"{n:X}")
 * and bytes.fromhex(f"0{e:X}") produce for these keys) and the 2 bytes of
 * the code's last 4 hex digits. Returns ESP_ERR_INVALID_ARG when the code is
 * malformed or the hash's first byte differs from the code's first 2 hex
 * digits (out is still filled in the latter case), ESP_OK when it matches. */
esp_err_t atv_pair_secret(const mbedtls_mpi *client_n, const mbedtls_mpi *client_e,
                          const mbedtls_mpi *server_n, const mbedtls_mpi *server_e,
                          const char *code, uint8_t out[ATV_SECRET_LEN]);

#ifdef __cplusplus
}
#endif
