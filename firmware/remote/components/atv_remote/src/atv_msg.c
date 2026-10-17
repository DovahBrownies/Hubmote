#include "atv_msg.h"

#include <string.h>

/* ------------------------------------------------------- RemoteMessage out */

/* RemoteMessage field numbers (remotemessage.proto) */
#define RM_REMOTE_CONFIGURE              1
#define RM_REMOTE_SET_ACTIVE             2
#define RM_REMOTE_ERROR                  3
#define RM_REMOTE_PING_REQUEST           8
#define RM_REMOTE_PING_RESPONSE          9
#define RM_REMOTE_KEY_INJECT             10
#define RM_REMOTE_IME_KEY_INJECT         20
#define RM_REMOTE_IME_BATCH_EDIT         21
#define RM_REMOTE_IME_SHOW_REQUEST       22
#define RM_REMOTE_VOICE_BEGIN            30
#define RM_REMOTE_VOICE_PAYLOAD          31
#define RM_REMOTE_VOICE_END              32
#define RM_REMOTE_START                  40
#define RM_REMOTE_SET_VOLUME_LEVEL       50
#define RM_REMOTE_ADJUST_VOLUME_LEVEL    51
#define RM_REMOTE_SET_PREFERRED_AUDIO    60
#define RM_REMOTE_RESET_PREFERRED_AUDIO  61
#define RM_REMOTE_APP_LINK_LAUNCH        90

/* OuterMessage field numbers (polo.proto) */
#define OM_PROTOCOL_VERSION    1
#define OM_STATUS              2
#define OM_PAIRING_REQUEST     10
#define OM_PAIRING_REQUEST_ACK 11
#define OM_OPTIONS             20
#define OM_CONFIGURATION       30
#define OM_CONFIGURATION_ACK   31
#define OM_SECRET              40
#define OM_SECRET_ACK          41

bool atv_msg_key_inject(pb_writer_t *w, int32_t key_code, int32_t direction)
{
    size_t m = pb_begin(w, RM_REMOTE_KEY_INJECT);
    pb_put_int32_p3(w, 1, key_code);  /* RemoteKeyCode key_code */
    pb_put_int32_p3(w, 2, direction); /* RemoteDirection direction */
    pb_end(w, m);
    return !w->overflow;
}

bool atv_msg_ping_response(pb_writer_t *w, int32_t val1)
{
    size_t m = pb_begin(w, RM_REMOTE_PING_RESPONSE);
    pb_put_int32_p3(w, 1, val1);
    pb_end(w, m);
    return !w->overflow;
}

bool atv_msg_configure(pb_writer_t *w, int32_t code1)
{
    size_t m = pb_begin(w, RM_REMOTE_CONFIGURE);
    pb_put_int32_p3(w, 1, code1);
    size_t d = pb_begin(w, 2); /* RemoteDeviceInfo device_info */
    /* model (1) and vendor (2) are left empty, as remote.py does. */
    pb_put_int32_p3(w, 3, ATV_DEVINFO_UNKNOWN1);
    pb_put_string_p3(w, 4, ATV_DEVINFO_UNKNOWN2);
    pb_put_string_p3(w, 5, ATV_DEVINFO_PACKAGE_NAME);
    pb_put_string_p3(w, 6, ATV_DEVINFO_APP_VERSION);
    pb_end(w, d);
    pb_end(w, m);
    return !w->overflow;
}

bool atv_msg_set_active(pb_writer_t *w, int32_t active)
{
    size_t m = pb_begin(w, RM_REMOTE_SET_ACTIVE);
    pb_put_int32_p3(w, 1, active);
    pb_end(w, m);
    return !w->overflow;
}

bool atv_msg_app_link(pb_writer_t *w, const char *app_link)
{
    size_t m = pb_begin(w, RM_REMOTE_APP_LINK_LAUNCH);
    pb_put_string_p3(w, 1, app_link);
    pb_end(w, m);
    return !w->overflow;
}

size_t atv_utf8_len(const char *s)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

/* urllib.parse.urlsplit: a scheme is present when there is a ':' at i > 0,
 * url[0] is an ASCII letter and url[:i] only holds letters, digits, "+-.". */
static bool has_url_scheme(const char *s)
{
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) {
        return false;
    }
    for (const char *p = s + 1; *p; p++) {
        char c = *p;
        if (c == ':') {
            return true;
        }
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' ||
                  c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return false;
}

size_t atv_app_link_build(const char *link_or_id, char *out, size_t out_len)
{
    static const char prefix[] = "market://launch?id=";
    const char *pre = has_url_scheme(link_or_id) ? "" : prefix;
    size_t pl = strlen(pre), ll = strlen(link_or_id);
    if (pl + ll + 1 > out_len) {
        return 0;
    }
    memcpy(out, pre, pl);
    memcpy(out + pl, link_or_id, ll + 1);
    return pl + ll;
}

bool atv_msg_ime_batch_edit(pb_writer_t *w, int32_t ime_counter, int32_t field_counter, const char *text)
{
    int32_t pos = (int32_t)atv_utf8_len(text) - 1;
    size_t m = pb_begin(w, RM_REMOTE_IME_BATCH_EDIT);
    pb_put_int32_p3(w, 1, ime_counter);
    pb_put_int32_p3(w, 2, field_counter);
    size_t e = pb_begin(w, 3); /* repeated RemoteEditInfo edit_info: one entry */
    pb_put_int32_p3(w, 1, 1);  /* insert = 1 */
    size_t o = pb_begin(w, 2); /* RemoteImeObject text_field_status */
    pb_put_int32_p3(w, 1, pos); /* start */
    pb_put_int32_p3(w, 2, pos); /* end */
    pb_put_string_p3(w, 3, text);
    pb_end(w, o);
    pb_end(w, e);
    pb_end(w, m);
    return !w->overflow;
}

/* --------------------------------------------------------- OuterMessage out */

static void polo_header(pb_writer_t *w)
{
    /* proto2 required fields: always serialized. */
    pb_put_uint(w, OM_PROTOCOL_VERSION, POLO_PROTOCOL_VERSION);
    pb_put_uint(w, OM_STATUS, POLO_STATUS_OK);
}

bool atv_polo_pairing_request(pb_writer_t *w, const char *client_name)
{
    polo_header(w);
    size_t m = pb_begin(w, OM_PAIRING_REQUEST);
    pb_put_string(w, 1, POLO_SERVICE_NAME); /* service_name */
    pb_put_string(w, 2, client_name);       /* client_name */
    pb_end(w, m);
    return !w->overflow;
}

bool atv_polo_options(pb_writer_t *w)
{
    polo_header(w);
    size_t m = pb_begin(w, OM_OPTIONS);
    size_t e = pb_begin(w, 1); /* repeated Encoding input_encodings */
    pb_put_uint(w, 1, POLO_ENCODING_HEXADECIMAL);
    pb_put_uint(w, 2, POLO_SYMBOL_LENGTH);
    pb_end(w, e);
    pb_put_uint(w, 3, POLO_ROLE_INPUT); /* preferred_role */
    pb_end(w, m);
    return !w->overflow;
}

bool atv_polo_configuration(pb_writer_t *w)
{
    polo_header(w);
    size_t m = pb_begin(w, OM_CONFIGURATION);
    size_t e = pb_begin(w, 1); /* Encoding encoding */
    pb_put_uint(w, 1, POLO_ENCODING_HEXADECIMAL);
    pb_put_uint(w, 2, POLO_SYMBOL_LENGTH);
    pb_end(w, e);
    pb_put_uint(w, 2, POLO_ROLE_INPUT); /* client_role */
    pb_end(w, m);
    return !w->overflow;
}

bool atv_polo_secret(pb_writer_t *w, const uint8_t *secret, size_t len)
{
    polo_header(w);
    size_t m = pb_begin(w, OM_SECRET);
    pb_put_bytes(w, 1, secret, len);
    pb_end(w, m);
    return !w->overflow;
}

/* ----------------------------------------------------------------- framing */

uint8_t *atv_frame_finish(uint8_t *buf, size_t body_len, size_t *frame_len)
{
    uint8_t tmp[PB_VARINT_MAX];
    size_t n = pb_varint_encode(body_len, tmp);
    uint8_t *start = buf + PB_VARINT_MAX - n;
    memcpy(start, tmp, n);
    *frame_len = n + body_len;
    return start;
}

/* ------------------------------------------------------- RemoteMessage in */

static inline int32_t as_int32(const pb_field_t *f)
{
    return (int32_t)(uint32_t)f->varint;
}

static bool parse_text_field(const pb_field_t *outer, atv_text_field_t *tf)
{
    pb_reader_t r;
    pb_field_t f;
    pb_reader_init(&r, outer->data, outer->len);
    while (pb_next(&r, &f)) {
        switch (f.field) {
        case 1: if (f.wire_type == PB_WT_VARINT) tf->counter_field = as_int32(&f); break;
        case 2: if (f.wire_type == PB_WT_LEN) pb_copy_string(&f, tf->value, sizeof(tf->value)); break;
        case 3: if (f.wire_type == PB_WT_VARINT) tf->start = as_int32(&f); break;
        case 4: if (f.wire_type == PB_WT_VARINT) tf->end = as_int32(&f); break;
        case 6: if (f.wire_type == PB_WT_LEN) pb_copy_string(&f, tf->label, sizeof(tf->label)); break;
        default: break;
        }
    }
    return !r.error;
}

static bool parse_device_info(const pb_field_t *outer, atv_remote_msg_t *m)
{
    pb_reader_t r;
    pb_field_t f;
    pb_reader_init(&r, outer->data, outer->len);
    while (pb_next(&r, &f)) {
        if (f.wire_type != PB_WT_LEN) {
            continue;
        }
        switch (f.field) {
        case 1: pb_copy_string(&f, m->model, sizeof(m->model)); break;
        case 2: pb_copy_string(&f, m->vendor, sizeof(m->vendor)); break;
        case 5: pb_copy_string(&f, m->package_name, sizeof(m->package_name)); break;
        case 6: pb_copy_string(&f, m->app_version, sizeof(m->app_version)); break;
        default: break;
        }
    }
    return !r.error;
}

static bool parse_sub(const pb_field_t *outer, atv_remote_msg_t *m)
{
    pb_reader_t r;
    pb_field_t f;
    pb_reader_init(&r, outer->data, outer->len);

    switch (outer->field) {
    case RM_REMOTE_CONFIGURE:
        while (pb_next(&r, &f)) {
            if (f.field == 1 && f.wire_type == PB_WT_VARINT) {
                m->configure_code1 = as_int32(&f);
            } else if (f.field == 2 && f.wire_type == PB_WT_LEN) {
                m->has_device_info = true;
                if (!parse_device_info(&f, m)) {
                    return false;
                }
            }
        }
        break;
    case RM_REMOTE_SET_ACTIVE:
        while (pb_next(&r, &f)) {
            if (f.field == 1 && f.wire_type == PB_WT_VARINT) {
                m->set_active = as_int32(&f);
            }
        }
        break;
    case RM_REMOTE_ERROR:
        while (pb_next(&r, &f)) {
            if (f.field == 1 && f.wire_type == PB_WT_VARINT) {
                m->error_value = f.varint != 0;
            } else if (f.field == 2 && f.wire_type == PB_WT_LEN) {
                m->error_has_message = true;
            }
        }
        break;
    case RM_REMOTE_PING_REQUEST:
        while (pb_next(&r, &f)) {
            if (f.wire_type != PB_WT_VARINT) {
                continue;
            }
            if (f.field == 1) {
                m->ping_val1 = as_int32(&f);
            } else if (f.field == 2) {
                m->ping_val2 = as_int32(&f);
            }
        }
        break;
    case RM_REMOTE_KEY_INJECT:
        while (pb_next(&r, &f)) {
            if (f.wire_type != PB_WT_VARINT) {
                continue;
            }
            if (f.field == 1) {
                m->key_code = as_int32(&f);
            } else if (f.field == 2) {
                m->key_direction = as_int32(&f);
            }
        }
        break;
    case RM_REMOTE_IME_KEY_INJECT:
        while (pb_next(&r, &f)) {
            if (f.wire_type != PB_WT_LEN) {
                continue;
            }
            if (f.field == 1) { /* RemoteAppInfo app_info */
                m->has_app_info = true;
                pb_reader_t ar;
                pb_field_t af;
                pb_reader_init(&ar, f.data, f.len);
                while (pb_next(&ar, &af)) {
                    if (af.field == 12 && af.wire_type == PB_WT_LEN) {
                        pb_copy_string(&af, m->app_package, sizeof(m->app_package));
                    }
                }
                if (ar.error) {
                    return false;
                }
            } else if (f.field == 2) { /* RemoteTextFieldStatus text_field_status */
                m->has_ime_text_field = true;
                if (!parse_text_field(&f, &m->ime_text_field)) {
                    return false;
                }
            }
        }
        break;
    case RM_REMOTE_IME_BATCH_EDIT:
        while (pb_next(&r, &f)) {
            if (f.wire_type != PB_WT_VARINT) {
                continue;
            }
            if (f.field == 1) {
                m->ime_counter = as_int32(&f);
            } else if (f.field == 2) {
                m->field_counter = as_int32(&f);
            }
        }
        break;
    case RM_REMOTE_IME_SHOW_REQUEST:
        while (pb_next(&r, &f)) {
            if (f.field == 2 && f.wire_type == PB_WT_LEN) {
                m->has_show_text_field = true;
                if (!parse_text_field(&f, &m->show_text_field)) {
                    return false;
                }
            }
        }
        break;
    case RM_REMOTE_VOICE_BEGIN:
        while (pb_next(&r, &f)) {
            if (f.field == 1 && f.wire_type == PB_WT_VARINT) {
                m->voice_session_id = as_int32(&f);
            }
        }
        break;
    case RM_REMOTE_START:
        while (pb_next(&r, &f)) {
            if (f.field == 1 && f.wire_type == PB_WT_VARINT) {
                m->started = f.varint != 0;
            }
        }
        break;
    case RM_REMOTE_SET_VOLUME_LEVEL:
        while (pb_next(&r, &f)) {
            switch (f.field) {
            case 3: if (f.wire_type == PB_WT_LEN) pb_copy_string(&f, m->player_model, sizeof(m->player_model)); break;
            case 6: if (f.wire_type == PB_WT_VARINT) m->volume_max = (uint32_t)f.varint; break;
            case 7: if (f.wire_type == PB_WT_VARINT) m->volume_level = (uint32_t)f.varint; break;
            case 8: if (f.wire_type == PB_WT_VARINT) m->volume_muted = f.varint != 0; break;
            default: break;
            }
        }
        break;
    default:
        /* Present but content not needed (or unknown). Still validate. */
        while (pb_next(&r, &f)) {
        }
        break;
    }
    return !r.error;
}

static uint32_t remote_field_bit(uint32_t field)
{
    switch (field) {
    case RM_REMOTE_CONFIGURE: return ATV_RM_CONFIGURE;
    case RM_REMOTE_SET_ACTIVE: return ATV_RM_SET_ACTIVE;
    case RM_REMOTE_ERROR: return ATV_RM_ERROR;
    case RM_REMOTE_PING_REQUEST: return ATV_RM_PING_REQUEST;
    case RM_REMOTE_PING_RESPONSE: return ATV_RM_PING_RESPONSE;
    case RM_REMOTE_KEY_INJECT: return ATV_RM_KEY_INJECT;
    case RM_REMOTE_IME_KEY_INJECT: return ATV_RM_IME_KEY_INJECT;
    case RM_REMOTE_IME_BATCH_EDIT: return ATV_RM_IME_BATCH_EDIT;
    case RM_REMOTE_IME_SHOW_REQUEST: return ATV_RM_IME_SHOW_REQUEST;
    case RM_REMOTE_VOICE_BEGIN: return ATV_RM_VOICE_BEGIN;
    case RM_REMOTE_VOICE_PAYLOAD: return ATV_RM_VOICE_PAYLOAD;
    case RM_REMOTE_VOICE_END: return ATV_RM_VOICE_END;
    case RM_REMOTE_START: return ATV_RM_START;
    case RM_REMOTE_SET_VOLUME_LEVEL: return ATV_RM_SET_VOLUME_LEVEL;
    case RM_REMOTE_ADJUST_VOLUME_LEVEL: return ATV_RM_ADJUST_VOLUME_LEVEL;
    case RM_REMOTE_SET_PREFERRED_AUDIO: return ATV_RM_SET_PREFERRED_AUDIO;
    case RM_REMOTE_RESET_PREFERRED_AUDIO: return ATV_RM_RESET_PREFERRED_AUDIO;
    case RM_REMOTE_APP_LINK_LAUNCH: return ATV_RM_APP_LINK_LAUNCH;
    default: return 0;
    }
}

bool atv_parse_remote(const uint8_t *buf, size_t len, atv_remote_msg_t *out)
{
    memset(out, 0, sizeof(*out));
    pb_reader_t r;
    pb_field_t f;
    pb_reader_init(&r, buf, len);
    while (pb_next(&r, &f)) {
        uint32_t bit = remote_field_bit(f.field);
        if (!bit || f.wire_type != PB_WT_LEN) {
            continue; /* unknown field: skipped */
        }
        out->present |= bit;
        if (!parse_sub(&f, out)) {
            return false;
        }
    }
    return !r.error;
}

/* ------------------------------------------------------- OuterMessage in */

bool atv_parse_polo(const uint8_t *buf, size_t len, atv_polo_msg_t *out)
{
    memset(out, 0, sizeof(*out));
    pb_reader_t r;
    pb_field_t f;
    pb_reader_init(&r, buf, len);
    while (pb_next(&r, &f)) {
        if (f.wire_type == PB_WT_VARINT) {
            if (f.field == OM_PROTOCOL_VERSION) {
                out->has_protocol_version = true;
                out->protocol_version = (uint32_t)f.varint;
            } else if (f.field == OM_STATUS) {
                out->has_status = true;
                out->status = (uint32_t)f.varint;
            }
            continue;
        }
        if (f.wire_type != PB_WT_LEN) {
            continue;
        }
        switch (f.field) {
        case OM_PAIRING_REQUEST: out->has_pairing_request = true; break;
        case OM_PAIRING_REQUEST_ACK: {
            out->has_pairing_request_ack = true;
            pb_reader_t ar;
            pb_field_t af;
            pb_reader_init(&ar, f.data, f.len);
            while (pb_next(&ar, &af)) {
                if (af.field == 1 && af.wire_type == PB_WT_LEN) {
                    pb_copy_string(&af, out->server_name, sizeof(out->server_name));
                }
            }
            if (ar.error) {
                return false;
            }
            break;
        }
        case OM_OPTIONS: out->has_options = true; break;
        case OM_CONFIGURATION: out->has_configuration = true; break;
        case OM_CONFIGURATION_ACK: out->has_configuration_ack = true; break;
        case OM_SECRET: out->has_secret = true; break;
        case OM_SECRET_ACK: out->has_secret_ack = true; break;
        default: break;
        }
    }
    return !r.error;
}
