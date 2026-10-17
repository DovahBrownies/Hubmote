#pragma once

/*
 * Client identity (RSA-2048 key + self-signed X.509 certificate, both PEM,
 * stored in NVS) and the pairing secret computation of pairing.py.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "mbedtls/bignum.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "atv_secret.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the locks. Call once from atv_init. */
esp_err_t atv_id_init(void);

/* True when both the key and the certificate are stored. */
bool atv_id_exists(const char *nvs_ns);

/* Blocking, slow. Generates the RSA-2048 key (e = 65537) and a self-signed
 * certificate modeled on certificate_generator.py (CN = common_name,
 * SAN DNS = common_name, BasicConstraints CA:true pathlen:0 non-critical,
 * serial 1000, SHA-256, valid 10 years) and stores both. No-op when an
 * identity already exists. */
esp_err_t atv_id_generate(const char *nvs_ns, const char *common_name);

/* Loads the stored identity into crt/pk (both must be initialized). */
esp_err_t atv_id_load(const char *nvs_ns, mbedtls_x509_crt *crt, mbedtls_pk_context *pk,
                      int (*f_rng)(void *, unsigned char *, size_t), void *p_rng);

/* atv_pair_secret() with the RSA public keys taken from the two certificates. Returns
 * ESP_ERR_NOT_SUPPORTED when either certificate does not hold an RSA key. */
esp_err_t atv_pair_secret_from_certs(const mbedtls_x509_crt *client, const mbedtls_x509_crt *server,
                                     const char *code, uint8_t out[ATV_SECRET_LEN]);

#ifdef __cplusplus
}
#endif
