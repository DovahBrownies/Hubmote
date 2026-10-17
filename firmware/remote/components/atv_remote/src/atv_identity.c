#include "atv_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/oid.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/rsa.h"
#include "nvs.h"

static const char *TAG = "atv_id";

#define NVS_KEY_PRIV "key"  /* PEM, PKCS#1 "RSA PRIVATE KEY" (TraditionalOpenSSL) */
#define NVS_KEY_CERT "cert" /* PEM certificate */

#define RSA_BITS     2048
#define RSA_EXPONENT 65537
#define PEM_BUF_LEN  4096
#define CN_MAX_BYTES 64 /* X.520 ub-common-name */

static SemaphoreHandle_t s_gen_lock;   /* one generation at a time */
static SemaphoreHandle_t s_store_lock; /* NVS read/write of the key + cert pair */

esp_err_t atv_id_init(void)
{
    if (!s_gen_lock) {
        s_gen_lock = xSemaphoreCreateMutex();
    }
    if (!s_store_lock) {
        s_store_lock = xSemaphoreCreateMutex();
    }
    return (s_gen_lock && s_store_lock) ? ESP_OK : ESP_ERR_NO_MEM;
}

static void *calloc_prefer_psram(size_t n, size_t size)
{
    return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
}

/* ------------------------------------------------------------------ NVS */

static bool blob_exists(nvs_handle_t h, const char *key)
{
    size_t len = 0;
    return nvs_get_blob(h, key, NULL, &len) == ESP_OK && len > 0;
}

bool atv_id_exists(const char *nvs_ns)
{
    nvs_handle_t h;
    if (!nvs_ns || nvs_open(nvs_ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = blob_exists(h, NVS_KEY_PRIV) && blob_exists(h, NVS_KEY_CERT);
    nvs_close(h);
    return ok;
}

/* Reads a PEM blob and NUL-terminates it (mbedTLS wants the NUL counted). */
static esp_err_t read_pem(nvs_handle_t h, const char *key, unsigned char **out, size_t *out_len)
{
    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, key, NULL, &len);
    if (err != ESP_OK) {
        return err;
    }
    unsigned char *buf = malloc(len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_blob(h, key, buf, &len);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len + 1;
    return ESP_OK;
}

esp_err_t atv_id_load(const char *nvs_ns, mbedtls_x509_crt *crt, mbedtls_pk_context *pk,
                      int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    unsigned char *key_pem = NULL, *cert_pem = NULL;
    size_t key_len = 0, cert_len = 0;
    nvs_handle_t h;

    xSemaphoreTake(s_store_lock, portMAX_DELAY);
    esp_err_t err = nvs_open(nvs_ns, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = read_pem(h, NVS_KEY_PRIV, &key_pem, &key_len);
        if (err == ESP_OK) {
            err = read_pem(h, NVS_KEY_CERT, &cert_pem, &cert_len);
        }
        nvs_close(h);
    }
    xSemaphoreGive(s_store_lock);

    if (err == ESP_OK) {
        int ret = mbedtls_x509_crt_parse(crt, cert_pem, cert_len);
        if (ret != 0) {
            ESP_LOGE(TAG, "stored certificate does not parse (-0x%04x)", (unsigned)-ret);
            err = ESP_ERR_INVALID_STATE;
        }
    }
    if (err == ESP_OK) {
        int ret = mbedtls_pk_parse_key(pk, key_pem, key_len, NULL, 0, f_rng, p_rng);
        if (ret != 0) {
            ESP_LOGE(TAG, "stored key does not parse (-0x%04x)", (unsigned)-ret);
            err = ESP_ERR_INVALID_STATE;
        }
    }
    if (key_pem) {
        mbedtls_platform_zeroize(key_pem, key_len);
        free(key_pem);
    }
    free(cert_pem);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_ERR_NOT_FOUND;
    }
    return err;
}

/* ----------------------------------------------------------- generation */

typedef struct {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context pk;
    mbedtls_x509write_cert crt;
    unsigned char key_pem[PEM_BUF_LEN];
    unsigned char cert_pem[PEM_BUF_LEN];
    char subject[8 + 3 * CN_MAX_BYTES];
    unsigned char san[4 + CN_MAX_BYTES];
} gen_ctx_t;

/* Subject "CN=<name>" for mbedtls_x509write_crt_set_subject_name. Every byte
 * outside [A-Za-z0-9 ._-] is written as an RFC 4514 \XX escape so commas,
 * '=' and friends cannot split the DN. A leading space is escaped too. */
static void build_subject(const char *name, char *out, size_t out_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = (size_t)snprintf(out, out_len, "CN=");
    size_t in_len = strlen(name);
    if (in_len > CN_MAX_BYTES) {
        in_len = CN_MAX_BYTES;
        while (in_len > 0 && ((unsigned char)name[in_len] & 0xC0) == 0x80) {
            in_len--; /* keep whole UTF-8 characters */
        }
    }
    for (size_t i = 0; i < in_len && o + 4 < out_len; i++) {
        unsigned char c = (unsigned char)name[i];
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                     c == '_' || c == '-' || (c == ' ' && i > 0 && i + 1 < in_len);
        if (plain) {
            out[o++] = (char)c;
        } else {
            out[o++] = '\\';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

/* SubjectAltName extension value: GeneralNames { dNSName [2] IA5String }.
 * IA5 is ASCII, so non-printable / non-ASCII bytes are dropped. */
static size_t build_san(const char *name, unsigned char *out)
{
    size_t n = 0;
    for (const char *p = name; *p && n < CN_MAX_BYTES; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x20 && c < 0x7F) {
            out[4 + n++] = c;
        }
    }
    if (n == 0) {
        memcpy(out + 4, "atvremote", 9);
        n = 9;
    }
    out[0] = 0x30; /* SEQUENCE */
    out[1] = (unsigned char)(n + 2);
    out[2] = 0x82; /* [2] dNSName, implicit IA5String */
    out[3] = (unsigned char)n;
    return n + 4;
}

static void format_time(time_t t, char out[16])
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, 16, "%Y%m%d%H%M%S", &tm);
}

esp_err_t atv_id_generate(const char *nvs_ns, const char *common_name)
{
    if (!nvs_ns || !common_name) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_gen_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_gen_lock, portMAX_DELAY);
    if (atv_id_exists(nvs_ns)) {
        xSemaphoreGive(s_gen_lock);
        return ESP_OK;
    }

    esp_err_t err = ESP_FAIL;
    int ret = 0;
    const char *step = "alloc";
    gen_ctx_t *g = calloc_prefer_psram(1, sizeof(*g));
    if (!g) {
        xSemaphoreGive(s_gen_lock);
        return ESP_ERR_NO_MEM;
    }
    mbedtls_entropy_init(&g->entropy);
    mbedtls_ctr_drbg_init(&g->drbg);
    mbedtls_pk_init(&g->pk);
    mbedtls_x509write_crt_init(&g->crt);

    int64_t t0 = esp_log_timestamp();
    ESP_LOGI(TAG, "generating RSA-%d identity (this takes a while)", RSA_BITS);

    static const unsigned char pers[] = "atv_remote identity";
    step = "drbg seed";
    ret = mbedtls_ctr_drbg_seed(&g->drbg, mbedtls_entropy_func, &g->entropy, pers, sizeof(pers) - 1);
    if (ret != 0) {
        goto out;
    }
    step = "pk setup";
    ret = mbedtls_pk_setup(&g->pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        goto out;
    }
    step = "rsa keygen";
    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(g->pk), mbedtls_ctr_drbg_random, &g->drbg, RSA_BITS, RSA_EXPONENT);
    if (ret != 0) {
        goto out;
    }
    ESP_LOGI(TAG, "RSA key generated in %lu ms", (unsigned long)(esp_log_timestamp() - t0));

    /* Certificate, modeled on certificate_generator.py. */
    build_subject(common_name, g->subject, sizeof(g->subject));
    size_t san_len = build_san(common_name, g->san);

    /* not_valid_before: now (backdated a day for clock skew), or a fixed
     * date when the RTC has not been set yet; not_valid_after: +10 years. */
    time_t now = time(NULL);
    if (now < 1704067200) { /* before 2024-01-01: clock not set */
        now = 1735689600;   /* 2025-01-01T00:00:00Z */
    } else {
        now -= 86400;
    }
    char not_before[16], not_after[16];
    format_time(now, not_before);
    format_time(now + (time_t)10 * 365 * 86400, not_after);

    static const unsigned char serial[] = {0x03, 0xE8}; /* 1000 */
    static const unsigned char basic_constraints[] = {0x30, 0x06, 0x01, 0x01, 0xFF, 0x02, 0x01, 0x00};

    mbedtls_x509write_crt_set_version(&g->crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&g->crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&g->crt, &g->pk);
    mbedtls_x509write_crt_set_issuer_key(&g->crt, &g->pk);
    step = "subject";
    if ((ret = mbedtls_x509write_crt_set_subject_name(&g->crt, g->subject)) != 0 ||
        (ret = mbedtls_x509write_crt_set_issuer_name(&g->crt, g->subject)) != 0) {
        goto out;
    }
    step = "serial";
    if ((ret = mbedtls_x509write_crt_set_serial_raw(&g->crt, (unsigned char *)serial, sizeof(serial))) != 0) {
        goto out;
    }
    step = "validity";
    if ((ret = mbedtls_x509write_crt_set_validity(&g->crt, not_before, not_after)) != 0) {
        goto out;
    }
    step = "extensions";
    if ((ret = mbedtls_x509write_crt_set_extension(&g->crt, MBEDTLS_OID_BASIC_CONSTRAINTS,
                                                   MBEDTLS_OID_SIZE(MBEDTLS_OID_BASIC_CONSTRAINTS), 0,
                                                   basic_constraints, sizeof(basic_constraints))) != 0 ||
        (ret = mbedtls_x509write_crt_set_extension(&g->crt, MBEDTLS_OID_SUBJECT_ALT_NAME,
                                                   MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME), 0, g->san,
                                                   san_len)) != 0) {
        goto out;
    }
    step = "cert pem";
    if ((ret = mbedtls_x509write_crt_pem(&g->crt, g->cert_pem, sizeof(g->cert_pem), mbedtls_ctr_drbg_random,
                                         &g->drbg)) != 0) {
        goto out;
    }
    step = "key pem";
    if ((ret = mbedtls_pk_write_key_pem(&g->pk, g->key_pem, sizeof(g->key_pem))) != 0) {
        goto out;
    }

    step = "nvs";
    nvs_handle_t h;
    xSemaphoreTake(s_store_lock, portMAX_DELAY);
    err = nvs_open(nvs_ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY_CERT, g->cert_pem, strlen((char *)g->cert_pem));
        if (err == ESP_OK) {
            err = nvs_set_blob(h, NVS_KEY_PRIV, g->key_pem, strlen((char *)g->key_pem));
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    xSemaphoreGive(s_store_lock);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "identity stored in NVS \"%s\" (%s, valid %s..%s), %lu ms total, caller stack never used: %u bytes",
                 nvs_ns, g->subject, not_before, not_after, (unsigned long)(esp_log_timestamp() - t0),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    } else {
        ESP_LOGE(TAG, "storing identity failed: %s", esp_err_to_name(err));
    }

out:
    if (ret != 0) {
        ESP_LOGE(TAG, "identity generation failed at %s (-0x%04x)", step, (unsigned)-ret);
        err = (ret == MBEDTLS_ERR_PK_ALLOC_FAILED || ret == MBEDTLS_ERR_X509_ALLOC_FAILED) ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    mbedtls_x509write_crt_free(&g->crt);
    mbedtls_pk_free(&g->pk);
    mbedtls_ctr_drbg_free(&g->drbg);
    mbedtls_entropy_free(&g->entropy);
    mbedtls_platform_zeroize(g, sizeof(*g));
    heap_caps_free(g);
    xSemaphoreGive(s_gen_lock);
    return err;
}

/* --------------------------------------------------------------- secret */

esp_err_t atv_pair_secret_from_certs(const mbedtls_x509_crt *client, const mbedtls_x509_crt *server,
                                     const char *code, uint8_t out[ATV_SECRET_LEN])
{
    if (!client || !server || !mbedtls_pk_can_do(&client->pk, MBEDTLS_PK_RSA) ||
        !mbedtls_pk_can_do(&server->pk, MBEDTLS_PK_RSA)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    mbedtls_mpi cn, ce, sn, se;
    mbedtls_mpi_init(&cn);
    mbedtls_mpi_init(&ce);
    mbedtls_mpi_init(&sn);
    mbedtls_mpi_init(&se);
    esp_err_t err = ESP_FAIL;
    if (mbedtls_rsa_export(mbedtls_pk_rsa(client->pk), &cn, NULL, NULL, NULL, &ce) == 0 &&
        mbedtls_rsa_export(mbedtls_pk_rsa(server->pk), &sn, NULL, NULL, NULL, &se) == 0) {
        err = atv_pair_secret(&cn, &ce, &sn, &se, code, out);
    }
    mbedtls_mpi_free(&cn);
    mbedtls_mpi_free(&ce);
    mbedtls_mpi_free(&sn);
    mbedtls_mpi_free(&se);
    return err;
}
