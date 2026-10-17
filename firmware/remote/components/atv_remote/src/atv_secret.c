#include "atv_secret.h"

#include <stdlib.h>
#include <string.h>

#include "mbedtls/sha256.h"

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool atv_code_valid(const char *code)
{
    if (!code || strlen(code) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (hexval(code[i]) < 0) {
            return false;
        }
    }
    return true;
}

/* Hashes X as minimal big-endian bytes (no sign byte, no padding).
 *
 * pairing.py formats n as bytes.fromhex(f"{n:X}") and e as
 * bytes.fromhex(f"0{e:X}"). For an RSA-2048 modulus the top bit is set, so
 * the hex has 512 digits and no leading zero: 256 minimal bytes. For
 * e = 65537 the hex is "10001" and the leading "0" makes "010001": the 3
 * minimal bytes 01 00 01. (Where the f-string would give an odd number of
 * digits, Python raises; minimal bytes are the only sensible equivalent.) */
static int hash_mpi(mbedtls_sha256_context *sha, const mbedtls_mpi *X)
{
    size_t n = mbedtls_mpi_size(X);
    if (n == 0 || n > 1024) {
        return MBEDTLS_ERR_MPI_BAD_INPUT_DATA;
    }
    unsigned char *buf = malloc(n);
    if (!buf) {
        return MBEDTLS_ERR_MPI_ALLOC_FAILED;
    }
    int ret = mbedtls_mpi_write_binary(X, buf, n);
    if (ret == 0) {
        ret = mbedtls_sha256_update(sha, buf, n);
    }
    free(buf);
    return ret;
}

esp_err_t atv_pair_secret(const mbedtls_mpi *client_n, const mbedtls_mpi *client_e,
                          const mbedtls_mpi *server_n, const mbedtls_mpi *server_e,
                          const char *code, uint8_t out[ATV_SECRET_LEN])
{
    if (!atv_code_valid(code)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t first = (uint8_t)(hexval(code[0]) << 4 | hexval(code[1]));
    /* bytes.fromhex(pairing_code[2:]) */
    uint8_t tail[2] = {
        (uint8_t)(hexval(code[2]) << 4 | hexval(code[3])),
        (uint8_t)(hexval(code[4]) << 4 | hexval(code[5])),
    };

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    int ret = mbedtls_sha256_starts(&sha, 0);
    if (ret == 0) {
        ret = hash_mpi(&sha, client_n);
    }
    if (ret == 0) {
        ret = hash_mpi(&sha, client_e);
    }
    if (ret == 0) {
        ret = hash_mpi(&sha, server_n);
    }
    if (ret == 0) {
        ret = hash_mpi(&sha, server_e);
    }
    if (ret == 0) {
        ret = mbedtls_sha256_update(&sha, tail, sizeof(tail));
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish(&sha, out);
    }
    mbedtls_sha256_free(&sha);
    if (ret != 0) {
        return ESP_FAIL;
    }
    return out[0] == first ? ESP_OK : ESP_ERR_INVALID_ARG;
}
