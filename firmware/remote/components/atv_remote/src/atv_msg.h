#pragma once

/*
 * Android TV Remote v2 messages: builders for what we send and parsers for
 * what the TV sends. Pure functions (no I/O, no globals) so the self-test
 * can check them against golden vectors from the official protobuf library.
 *
 * Control port 6466: RemoteMessage (remotemessage.proto, proto3).
 * Pairing port 6467: OuterMessage (polo.proto, proto2).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "atv_pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* remote.py Feature flags (remote_configure.code1 / remote_set_active.active). */
#define ATV_FEATURE_PING     (1 << 0)
#define ATV_FEATURE_KEY      (1 << 1)
#define ATV_FEATURE_IME      (1 << 2)
#define ATV_FEATURE_VOICE    (1 << 3)
#define ATV_FEATURE_UNKNOWN1 (1 << 4)
#define ATV_FEATURE_POWER    (1 << 5)
#define ATV_FEATURE_VOLUME   (1 << 6)
#define ATV_FEATURE_APP_LINK (1 << 9)

/* Device info we report in the remote_configure reply (remote.py). */
#define ATV_DEVINFO_UNKNOWN1     1
#define ATV_DEVINFO_UNKNOWN2     "1"
#define ATV_DEVINFO_PACKAGE_NAME "atvremote"
#define ATV_DEVINFO_APP_VERSION  "1.0.0"

/* polo.proto constants */
#define POLO_PROTOCOL_VERSION        2
#define POLO_STATUS_OK               200
#define POLO_STATUS_ERROR            400
#define POLO_STATUS_BAD_CONFIGURATION 401
#define POLO_STATUS_BAD_SECRET       402
#define POLO_ENCODING_HEXADECIMAL    3
#define POLO_ROLE_INPUT              1
#define POLO_SERVICE_NAME            "atvremote"
#define POLO_SYMBOL_LENGTH           6

/* ------------------------------------------------------- RemoteMessage out */

bool atv_msg_key_inject(pb_writer_t *w, int32_t key_code, int32_t direction);
bool atv_msg_ping_response(pb_writer_t *w, int32_t val1);
bool atv_msg_configure(pb_writer_t *w, int32_t code1);
bool atv_msg_set_active(pb_writer_t *w, int32_t active);
bool atv_msg_app_link(pb_writer_t *w, const char *app_link);
/* send_text() of remote.py: one RemoteEditInfo{insert=1} whose
 * RemoteImeObject has start = end = (code points in text) - 1. */
bool atv_msg_ime_batch_edit(pb_writer_t *w, int32_t ime_counter, int32_t field_counter, const char *text);

/* Number of Unicode code points in a UTF-8 string (Python len(str)). */
size_t atv_utf8_len(const char *s);

/* AndroidTVRemote.send_launch_app_command: a link without a URL scheme (as
 * urllib.parse.urlparse sees it) is an application id and becomes
 * "market://launch?id=<id>". Returns the length written to out, or 0 when
 * it does not fit. */
size_t atv_app_link_build(const char *link_or_id, char *out, size_t out_len);

/* --------------------------------------------------------- OuterMessage out */

bool atv_polo_pairing_request(pb_writer_t *w, const char *client_name);
bool atv_polo_options(pb_writer_t *w);
bool atv_polo_configuration(pb_writer_t *w);
bool atv_polo_secret(pb_writer_t *w, const uint8_t *secret, size_t len);

/* ----------------------------------------------------------------- framing */

/* Prefixes a message body with its varint length. The body must already
 * be at buf + PB_VARINT_MAX (body_len bytes); the prefix is written right
 * before it. Returns the frame start; *frame_len gets prefix + body. */
uint8_t *atv_frame_finish(uint8_t *buf, size_t body_len, size_t *frame_len);

/* ------------------------------------------------------- RemoteMessage in */

typedef struct {
    int32_t counter_field;
    char value[128];
    int32_t start;
    int32_t end;
    char label[64];
} atv_text_field_t;

/* Top-level RemoteMessage fields seen (bit = field presence). */
enum {
    ATV_RM_CONFIGURE = 1u << 0,         /* 1 */
    ATV_RM_SET_ACTIVE = 1u << 1,        /* 2 */
    ATV_RM_ERROR = 1u << 2,             /* 3 */
    ATV_RM_PING_REQUEST = 1u << 3,      /* 8 */
    ATV_RM_PING_RESPONSE = 1u << 4,     /* 9 */
    ATV_RM_KEY_INJECT = 1u << 5,        /* 10 */
    ATV_RM_IME_KEY_INJECT = 1u << 6,    /* 20 */
    ATV_RM_IME_BATCH_EDIT = 1u << 7,    /* 21 */
    ATV_RM_IME_SHOW_REQUEST = 1u << 8,  /* 22 */
    ATV_RM_VOICE_BEGIN = 1u << 9,       /* 30 */
    ATV_RM_VOICE_PAYLOAD = 1u << 10,    /* 31 */
    ATV_RM_VOICE_END = 1u << 11,        /* 32 */
    ATV_RM_START = 1u << 12,            /* 40 */
    ATV_RM_SET_VOLUME_LEVEL = 1u << 13, /* 50 */
    ATV_RM_ADJUST_VOLUME_LEVEL = 1u << 14, /* 51 */
    ATV_RM_SET_PREFERRED_AUDIO = 1u << 15, /* 60 */
    ATV_RM_RESET_PREFERRED_AUDIO = 1u << 16, /* 61 */
    ATV_RM_APP_LINK_LAUNCH = 1u << 17,  /* 90 */
};

typedef struct {
    uint32_t present; /* ATV_RM_* */

    /* remote_configure */
    int32_t configure_code1;
    bool has_device_info;
    char model[48];
    char vendor[32];
    char app_version[24];
    char package_name[48];

    /* remote_set_active */
    int32_t set_active;

    /* remote_error */
    bool error_value;
    bool error_has_message;

    /* remote_ping_request */
    int32_t ping_val1;
    int32_t ping_val2;

    /* remote_key_inject (echo / unusual) */
    int32_t key_code;
    int32_t key_direction;

    /* remote_ime_key_inject */
    bool has_app_info;
    char app_package[128];
    bool has_ime_text_field;
    atv_text_field_t ime_text_field;

    /* remote_ime_batch_edit */
    int32_t ime_counter;
    int32_t field_counter;

    /* remote_ime_show_request */
    bool has_show_text_field;
    atv_text_field_t show_text_field;

    /* remote_voice_begin */
    int32_t voice_session_id;

    /* remote_start */
    bool started;

    /* remote_set_volume_level */
    uint32_t volume_level;
    uint32_t volume_max;
    bool volume_muted;
    char player_model[32];
} atv_remote_msg_t;

/* Parses a RemoteMessage. Returns false on malformed wire data. */
bool atv_parse_remote(const uint8_t *buf, size_t len, atv_remote_msg_t *out);

/* ------------------------------------------------------- OuterMessage in */

typedef struct {
    bool has_protocol_version;
    uint32_t protocol_version;
    bool has_status;
    uint32_t status;
    bool has_pairing_request;
    bool has_pairing_request_ack;
    char server_name[64];
    bool has_options;
    bool has_configuration;
    bool has_configuration_ack;
    bool has_secret;
    bool has_secret_ack;
} atv_polo_msg_t;

bool atv_parse_polo(const uint8_t *buf, size_t len, atv_polo_msg_t *out);

#ifdef __cplusplus
}
#endif
