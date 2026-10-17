#include "atv_selftest.h"

#include <stdlib.h>
#include <string.h>

#include "atv_golden.h"
#include "atv_secret.h"
#include "atv_keycodes.h"
#include "atv_msg.h"
#include "atv_pb.h"
#include "esp_log.h"
#include "mbedtls/bignum.h"

static const char *TAG = "atv_test";

#define BUF_LEN 1024

static int s_fail;
static int s_pass;

#define CHECK(cond, ...)                 \
    do {                                 \
        if (cond) {                      \
            s_pass++;                    \
        } else {                         \
            ESP_LOGE(TAG, __VA_ARGS__);  \
            s_fail++;                    \
        }                                \
    } while (0)

static void expect_bytes(const char *name, const uint8_t *got, size_t got_len, const uint8_t *want, size_t want_len)
{
    if (got_len == want_len && memcmp(got, want, want_len) == 0) {
        s_pass++;
        ESP_LOGD(TAG, "%s: ok (%u bytes)", name, (unsigned)got_len);
        return;
    }
    s_fail++;
    ESP_LOGE(TAG, "%s: encoder output differs (got %u bytes, want %u)", name, (unsigned)got_len, (unsigned)want_len);
    ESP_LOGE(TAG, "got:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, got, got_len, ESP_LOG_ERROR);
    ESP_LOGE(TAG, "want:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, want, want_len, ESP_LOG_ERROR);
}

/* Encodes with `call` (which writes to pb_writer_t w) and compares with the golden array. */
#define ENC_CHECK(golden, call)                                              \
    do {                                                                     \
        pb_writer_init(&w, buf, BUF_LEN);                                    \
        bool ok_ = (call);                                                   \
        CHECK(ok_, "%s: encoder reported overflow", #golden);                \
        expect_bytes(#golden, buf, w.len, golden, sizeof(golden));           \
    } while (0)

static void make_long_text(char *out)
{
    for (int i = 0; i < 200; i++) {
        out[i] = (char)('a' + i % 26);
    }
    out[200] = '\0';
}

static void test_encoders(uint8_t *buf)
{
    pb_writer_t w;
    const int32_t all = ATV_FEATURE_PING | ATV_FEATURE_KEY | ATV_FEATURE_IME | ATV_FEATURE_POWER |
                        ATV_FEATURE_VOLUME | ATV_FEATURE_APP_LINK;
    CHECK(all == 615, "feature mask with IME is %d, want 615", (int)all);
    CHECK((all & ~ATV_FEATURE_IME) == 611, "feature mask without IME is %d, want 611", (int)(all & ~ATV_FEATURE_IME));

    ENC_CHECK(g_key_dpad_up_short, atv_msg_key_inject(&w, ATV_KEY_DPAD_UP, ATV_DIR_SHORT));
    ENC_CHECK(g_key_hdmi1_start_long, atv_msg_key_inject(&w, ATV_KEY_TV_INPUT_HDMI_1, ATV_DIR_START_LONG));
    ENC_CHECK(g_key_home_end_long, atv_msg_key_inject(&w, ATV_KEY_HOME, ATV_DIR_END_LONG));
    ENC_CHECK(g_key_unknown_unknown, atv_msg_key_inject(&w, ATV_KEY_UNKNOWN, ATV_DIR_UNKNOWN));

    ENC_CHECK(g_ping_resp_0, atv_msg_ping_response(&w, 0));
    ENC_CHECK(g_ping_resp_5, atv_msg_ping_response(&w, 5));
    ENC_CHECK(g_ping_resp_neg1, atv_msg_ping_response(&w, -1));
    ENC_CHECK(g_ping_resp_big, atv_msg_ping_response(&w, 123456789));

    ENC_CHECK(g_configure_615, atv_msg_configure(&w, all));
    ENC_CHECK(g_configure_611, atv_msg_configure(&w, all & ~ATV_FEATURE_IME));
    ENC_CHECK(g_set_active_615, atv_msg_set_active(&w, 615));

    ENC_CHECK(g_app_link_netflix, atv_msg_app_link(&w, "https://www.netflix.com/title/80057281"));
    char link[96];
    size_t ll = atv_app_link_build("com.google.android.youtube.tv", link, sizeof(link));
    CHECK(ll > 0 && strcmp(link, "market://launch?id=com.google.android.youtube.tv") == 0,
          "app id was not turned into a market:// link: %s", link);
    ENC_CHECK(g_app_link_market, atv_msg_app_link(&w, link));
    ll = atv_app_link_build("https://www.netflix.com/title/80057281", link, sizeof(link));
    CHECK(ll > 0 && strcmp(link, "https://www.netflix.com/title/80057281") == 0, "https link was modified: %s", link);
    ll = atv_app_link_build("market://launch?id=x", link, sizeof(link));
    CHECK(ll > 0 && strcmp(link, "market://launch?id=x") == 0, "market link was modified: %s", link);

    ENC_CHECK(g_text_hello, atv_msg_ime_batch_edit(&w, 3, 7, "Hello"));
    ENC_CHECK(g_text_h, atv_msg_ime_batch_edit(&w, 0, 0, "h"));
    CHECK(atv_utf8_len(G_TEXT_UTF8) == 7, "utf8 length of G_TEXT_UTF8 is %u, want 7",
          (unsigned)atv_utf8_len(G_TEXT_UTF8));
    ENC_CHECK(g_text_utf8, atv_msg_ime_batch_edit(&w, 1, 2, G_TEXT_UTF8));
    char long_text[201];
    make_long_text(long_text);
    ENC_CHECK(g_text_long, atv_msg_ime_batch_edit(&w, 300, 129, long_text));

    ENC_CHECK(g_polo_pairing_request, atv_polo_pairing_request(&w, "Hubmote"));
    ENC_CHECK(g_polo_options, atv_polo_options(&w));
    ENC_CHECK(g_polo_configuration, atv_polo_configuration(&w));
    uint8_t secret[32];
    for (int i = 0; i < 32; i++) {
        secret[i] = (uint8_t)i;
    }
    ENC_CHECK(g_polo_secret, atv_polo_secret(&w, secret, sizeof(secret)));

    /* Framing: body at buf + PB_VARINT_MAX, prefix written in front. */
    pb_writer_init(&w, buf + PB_VARINT_MAX, BUF_LEN - PB_VARINT_MAX);
    atv_msg_ime_batch_edit(&w, 300, 129, long_text);
    size_t flen = 0;
    uint8_t *f = atv_frame_finish(buf, w.len, &flen);
    expect_bytes("g_frame_text_long", f, flen, g_frame_text_long, sizeof(g_frame_text_long));
    pb_writer_init(&w, buf + PB_VARINT_MAX, BUF_LEN - PB_VARINT_MAX);
    atv_msg_key_inject(&w, ATV_KEY_DPAD_UP, ATV_DIR_SHORT);
    f = atv_frame_finish(buf, w.len, &flen);
    expect_bytes("g_frame_key", f, flen, g_frame_key, sizeof(g_frame_key));

    /* Overflow is reported, not written past the end. */
    uint8_t tiny[8];
    pb_writer_init(&w, tiny, sizeof(tiny));
    CHECK(!atv_msg_app_link(&w, "https://www.netflix.com/title/80057281"), "overflow not detected");
}

static void test_framing(uint8_t *buf)
{
    size_t hdr, mlen;
    const uint8_t *fr = g_frame_text_long;
    const size_t frl = sizeof(g_frame_text_long);

    CHECK(pb_frame_peek(fr, 0, &hdr, &mlen) == PB_FRAME_NEED_MORE && hdr == 0, "frame: empty buffer");
    CHECK(pb_frame_peek(fr, 1, &hdr, &mlen) == PB_FRAME_NEED_MORE && hdr == 0, "frame: split inside varint");
    CHECK(pb_frame_peek(fr, 2, &hdr, &mlen) == PB_FRAME_NEED_MORE && hdr == 2 && mlen == frl - 2,
          "frame: 2-byte prefix not decoded (hdr %u len %u)", (unsigned)hdr, (unsigned)mlen);
    CHECK(pb_frame_peek(fr, frl - 1, &hdr, &mlen) == PB_FRAME_NEED_MORE, "frame: one byte short");
    CHECK(pb_frame_peek(fr, frl, &hdr, &mlen) == PB_FRAME_OK && hdr == 2 && mlen == frl - 2, "frame: complete");

    /* Two frames back to back: the first is reported on its own. */
    memcpy(buf, g_frame_key, sizeof(g_frame_key));
    memcpy(buf + sizeof(g_frame_key), fr, frl);
    CHECK(pb_frame_peek(buf, sizeof(g_frame_key) + frl, &hdr, &mlen) == PB_FRAME_OK && hdr == 1 &&
              mlen == sizeof(g_frame_key) - 1,
          "frame: first of two");
    CHECK(pb_frame_peek(buf + sizeof(g_frame_key), frl, &hdr, &mlen) == PB_FRAME_OK && mlen == frl - 2,
          "frame: second of two");

    memset(buf, 0xFF, 11);
    CHECK(pb_frame_peek(buf, 11, &hdr, &mlen) == PB_FRAME_CORRUPT, "frame: 11-byte varint not rejected");

    /* UTF-8 safe truncation. */
    const uint8_t s[] = {'a', 0xC3, 0xA9, 'b'}; /* "aéb" */
    pb_field_t fld = {.field = 1, .wire_type = PB_WT_LEN, .data = s, .len = sizeof(s)};
    char out[3];
    pb_copy_string(&fld, out, sizeof(out)); /* room for 2 bytes: must not split é */
    CHECK(strcmp(out, "a") == 0, "utf-8 truncation produced \"%s\"", out);
}

static void test_remote_parser(atv_remote_msg_t *m)
{
    bool ok;

    ok = atv_parse_remote(g_in_configure, sizeof(g_in_configure), m);
    CHECK(ok && (m->present & ATV_RM_CONFIGURE) && m->configure_code1 == 639 && m->has_device_info &&
              strcmp(m->model, "Google TV Streamer") == 0 && strcmp(m->vendor, "Google") == 0 &&
              strcmp(m->app_version, "6.4.1") == 0 &&
              strcmp(m->package_name, "com.google.android.tv.remote.service") == 0,
          "parse remote_configure (code1 %d model '%s' vendor '%s' ver '%s')", (int)m->configure_code1, m->model,
          m->vendor, m->app_version);

    ok = atv_parse_remote(g_in_set_active, sizeof(g_in_set_active), m);
    CHECK(ok && m->present == ATV_RM_SET_ACTIVE && m->set_active == 622, "parse remote_set_active");

    ok = atv_parse_remote(g_in_ping, sizeof(g_in_ping), m);
    CHECK(ok && m->present == ATV_RM_PING_REQUEST && m->ping_val1 == 12 && m->ping_val2 == 34,
          "parse remote_ping_request");
    ok = atv_parse_remote(g_in_ping_neg, sizeof(g_in_ping_neg), m);
    CHECK(ok && m->ping_val1 == -7, "parse negative ping val1 (%d)", (int)m->ping_val1);

    ok = atv_parse_remote(g_in_ime_key_inject, sizeof(g_in_ime_key_inject), m);
    CHECK(ok && m->present == ATV_RM_IME_KEY_INJECT && m->has_app_info &&
              strcmp(m->app_package, "com.google.android.youtube.tv") == 0 && m->has_ime_text_field &&
              strcmp(m->ime_text_field.value, "abc") == 0 && strcmp(m->ime_text_field.label, "Search") == 0 &&
              m->ime_text_field.start == 3 && m->ime_text_field.end == 3 && m->ime_text_field.counter_field == 2,
          "parse remote_ime_key_inject (app '%s')", m->app_package);

    ok = atv_parse_remote(g_in_ime_batch_edit, sizeof(g_in_ime_batch_edit), m);
    CHECK(ok && m->present == ATV_RM_IME_BATCH_EDIT && m->ime_counter == 17 && m->field_counter == 42,
          "parse remote_ime_batch_edit");

    ok = atv_parse_remote(g_in_ime_show, sizeof(g_in_ime_show), m);
    CHECK(ok && m->present == ATV_RM_IME_SHOW_REQUEST && m->has_show_text_field &&
              strcmp(m->show_text_field.value, "hello") == 0 &&
              strcmp(m->show_text_field.label, "Search YouTube") == 0 && m->show_text_field.start == 5 &&
              m->show_text_field.end == 5,
          "parse remote_ime_show_request");

    ok = atv_parse_remote(g_in_start_true, sizeof(g_in_start_true), m);
    CHECK(ok && m->present == ATV_RM_START && m->started, "parse remote_start{true}");
    ok = atv_parse_remote(g_in_start_false, sizeof(g_in_start_false), m);
    CHECK(ok && m->present == ATV_RM_START && !m->started, "parse remote_start{false}");

    ok = atv_parse_remote(g_in_volume, sizeof(g_in_volume), m);
    CHECK(ok && m->present == ATV_RM_SET_VOLUME_LEVEL && m->volume_level == 27 && m->volume_max == 100 &&
              m->volume_muted && strcmp(m->player_model, "Living Room TV") == 0,
          "parse remote_set_volume_level");

    ok = atv_parse_remote(g_in_error, sizeof(g_in_error), m);
    CHECK(ok && m->present == ATV_RM_ERROR && m->error_value && m->error_has_message, "parse remote_error");

    ok = atv_parse_remote(g_in_unknown_fields, sizeof(g_in_unknown_fields), m);
    CHECK(ok && m->present == ATV_RM_START && m->started, "unknown fields not skipped");

    /* Our own outgoing messages parse back. */
    ok = atv_parse_remote(g_key_hdmi1_start_long, sizeof(g_key_hdmi1_start_long), m);
    CHECK(ok && m->present == ATV_RM_KEY_INJECT && m->key_code == ATV_KEY_TV_INPUT_HDMI_1 &&
              m->key_direction == ATV_DIR_START_LONG,
          "parse remote_key_inject");
    ok = atv_parse_remote(g_ping_resp_0, sizeof(g_ping_resp_0), m);
    CHECK(ok && m->present == ATV_RM_PING_RESPONSE, "empty submessage presence");

    /* Truncated input is rejected. */
    ok = atv_parse_remote(g_in_configure, sizeof(g_in_configure) - 3, m);
    CHECK(!ok, "truncated message not rejected");
}

static void test_polo_parser(void)
{
    atv_polo_msg_t p;
    bool ok;

    ok = atv_parse_polo(g_in_polo_req_ack, sizeof(g_in_polo_req_ack), &p);
    CHECK(ok && p.has_status && p.status == POLO_STATUS_OK && p.protocol_version == 2 && p.has_pairing_request_ack &&
              strcmp(p.server_name, "Google TV Streamer") == 0 && !p.has_options,
          "parse pairing_request_ack");
    ok = atv_parse_polo(g_in_polo_options, sizeof(g_in_polo_options), &p);
    CHECK(ok && p.status == POLO_STATUS_OK && p.has_options && !p.has_pairing_request_ack, "parse options");
    ok = atv_parse_polo(g_in_polo_config_ack, sizeof(g_in_polo_config_ack), &p);
    CHECK(ok && p.status == POLO_STATUS_OK && p.has_configuration_ack, "parse configuration_ack (empty)");
    ok = atv_parse_polo(g_in_polo_secret_ack, sizeof(g_in_polo_secret_ack), &p);
    CHECK(ok && p.status == POLO_STATUS_OK && p.has_secret_ack, "parse secret_ack");
    ok = atv_parse_polo(g_in_polo_bad_secret, sizeof(g_in_polo_bad_secret), &p);
    CHECK(ok && p.has_status && p.status == POLO_STATUS_BAD_SECRET && !p.has_secret_ack, "parse status 402");
    ok = atv_parse_polo(g_polo_pairing_request, sizeof(g_polo_pairing_request), &p);
    CHECK(ok && p.has_pairing_request, "parse our pairing_request");
}

static void test_secret(void)
{
    mbedtls_mpi cn, ce, sn, se;
    mbedtls_mpi_init(&cn);
    mbedtls_mpi_init(&ce);
    mbedtls_mpi_init(&sn);
    mbedtls_mpi_init(&se);
    int ret = mbedtls_mpi_read_binary(&cn, g_secret_client_n, sizeof(g_secret_client_n));
    ret |= mbedtls_mpi_read_binary(&sn, g_secret_server_n, sizeof(g_secret_server_n));
    ret |= mbedtls_mpi_lset(&ce, G_SECRET_E);
    ret |= mbedtls_mpi_lset(&se, G_SECRET_E);
    CHECK(ret == 0, "mpi setup failed");

    uint8_t out[ATV_SECRET_LEN];
    esp_err_t err = atv_pair_secret(&cn, &ce, &sn, &se, G_SECRET_CODE, out);
    CHECK(err == ESP_OK, "secret: code %s rejected (%s)", G_SECRET_CODE, esp_err_to_name(err));
    expect_bytes("g_secret_hash", out, sizeof(out), g_secret_hash, sizeof(g_secret_hash));

    char code[7];
    strcpy(code, G_SECRET_CODE);
    for (char *p = code; *p; p++) {
        if (*p >= 'A' && *p <= 'F') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
    CHECK(atv_pair_secret(&cn, &ce, &sn, &se, code, out) == ESP_OK, "secret: lowercase code rejected");

    /* Wrong first byte: hash is the same (it only covers the last 4 digits) but must be refused. */
    strcpy(code, G_SECRET_CODE);
    code[1] = (code[1] == '0') ? '1' : '0';
    CHECK(atv_pair_secret(&cn, &ce, &sn, &se, code, out) == ESP_ERR_INVALID_ARG, "secret: wrong code accepted");
    /* Wrong last digits: different hash, almost surely a mismatch with the first byte. */
    strcpy(code, G_SECRET_CODE);
    code[5] = (code[5] == '0') ? '1' : '0';
    err = atv_pair_secret(&cn, &ce, &sn, &se, code, out);
    CHECK(err == ESP_ERR_INVALID_ARG || err == ESP_OK, "secret: unexpected error %s", esp_err_to_name(err));
    CHECK(memcmp(out, g_secret_hash, sizeof(out)) != 0, "secret: last 4 digits do not enter the hash");

    CHECK(atv_code_valid("A1b2C3"), "code validation: valid code refused");
    CHECK(!atv_code_valid("A1B2C"), "code validation: 5 digits accepted");
    CHECK(!atv_code_valid("A1B2C3D"), "code validation: 7 digits accepted");
    CHECK(!atv_code_valid("A1B2CG"), "code validation: non-hex accepted");
    CHECK(!atv_code_valid(NULL), "code validation: NULL accepted");

    mbedtls_mpi_free(&cn);
    mbedtls_mpi_free(&ce);
    mbedtls_mpi_free(&sn);
    mbedtls_mpi_free(&se);
}

esp_err_t atv_selftest(void)
{
    s_fail = 0;
    s_pass = 0;
    uint8_t *buf = malloc(BUF_LEN);
    atv_remote_msg_t *m = malloc(sizeof(*m));
    if (!buf || !m) {
        free(buf);
        free(m);
        return ESP_ERR_NO_MEM;
    }
    test_encoders(buf);
    test_framing(buf);
    test_remote_parser(m);
    test_polo_parser();
    test_secret();
    free(buf);
    free(m);

    if (s_fail) {
        ESP_LOGE(TAG, "self-test FAILED: %d of %d checks", s_fail, s_fail + s_pass);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "self-test passed (%d checks)", s_pass);
    return ESP_OK;
}
