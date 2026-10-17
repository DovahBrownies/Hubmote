#pragma once

/*
 * Minimal protobuf wire-format encoder/decoder: just what the Android TV
 * Remote v2 messages need (varints, length-delimited fields, nested
 * messages). No code generation; callers write fields in field-number
 * order, which is what the official serializers do too, so the output is
 * byte-identical to python protobuf for the same message.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PB_WT_VARINT = 0,
    PB_WT_I64 = 1,
    PB_WT_LEN = 2,
    PB_WT_I32 = 5,
};

#define PB_VARINT_MAX 10

/* ---------------------------------------------------------------- writer */

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    bool overflow; /* sticky: set once anything did not fit */
} pb_writer_t;

void pb_writer_init(pb_writer_t *w, uint8_t *buf, size_t cap);

/* Number of bytes v takes as a varint (1..10). */
size_t pb_varint_size(uint64_t v);
/* Encodes v at out (must have PB_VARINT_MAX bytes); returns bytes written. */
size_t pb_varint_encode(uint64_t v, uint8_t *out);

void pb_put_varint(pb_writer_t *w, uint64_t v);
void pb_put_tag(pb_writer_t *w, uint32_t field, uint8_t wire_type);

/* Field writers that always emit the field (proto2 "has" semantics). */
void pb_put_uint(pb_writer_t *w, uint32_t field, uint64_t v);
/* int32/enum: negative values are sign-extended to 10 bytes, as protobuf does. */
void pb_put_int32(pb_writer_t *w, uint32_t field, int32_t v);
void pb_put_bytes(pb_writer_t *w, uint32_t field, const void *data, size_t len);
void pb_put_string(pb_writer_t *w, uint32_t field, const char *s);

/* proto3 implicit-presence variants: skip the field when it holds the
 * default value (0 / empty string), exactly like proto3 serializers. */
void pb_put_int32_p3(pb_writer_t *w, uint32_t field, int32_t v);
void pb_put_string_p3(pb_writer_t *w, uint32_t field, const char *s);

/* Nested message: pb_begin writes the tag and reserves one length byte;
 * pb_end patches the length (moving the body when it needs more than one
 * byte). Submessages are always emitted, even when empty, like a "set"
 * message field in protobuf. Returns the marker to pass to pb_end. */
size_t pb_begin(pb_writer_t *w, uint32_t field);
void pb_end(pb_writer_t *w, size_t marker);

/* ---------------------------------------------------------------- reader */

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
    bool error; /* malformed input seen */
} pb_reader_t;

typedef struct {
    uint32_t field;
    uint8_t wire_type;
    uint64_t varint;     /* PB_WT_VARINT (also I32/I64 raw little-endian value) */
    const uint8_t *data; /* PB_WT_LEN */
    size_t len;          /* PB_WT_LEN */
} pb_field_t;

void pb_reader_init(pb_reader_t *r, const uint8_t *buf, size_t len);

/* Returns true with the next field, false at the end or on malformed input
 * (then r->error is set). Unknown fields are returned too; callers simply
 * ignore field numbers they do not care about. */
bool pb_next(pb_reader_t *r, pb_field_t *f);

/* Decodes a varint from buf[0..len). Returns bytes consumed, 0 when the
 * buffer ends before the varint does, or -1 when it is longer than 10 bytes. */
int pb_varint_decode(const uint8_t *buf, size_t len, uint64_t *out);

/* Copies a length-delimited field into a NUL-terminated string, truncating
 * on a UTF-8 character boundary when it does not fit. */
void pb_copy_string(const pb_field_t *f, char *out, size_t out_len);

/* ----------------------------------------------------------------- frame */

typedef enum {
    PB_FRAME_NEED_MORE = 0, /* length prefix or body incomplete */
    PB_FRAME_OK = 1,        /* complete frame: *hdr_len + *msg_len bytes */
    PB_FRAME_CORRUPT = -1,  /* length prefix is not a valid varint */
} pb_frame_result_t;

/* Inspects the start of a receive buffer for one varint-length-prefixed
 * message (base.py framing). On PB_FRAME_OK and on PB_FRAME_NEED_MORE with a
 * complete prefix, *hdr_len and *msg_len are filled (*hdr_len = 0 when even
 * the prefix is incomplete). */
pb_frame_result_t pb_frame_peek(const uint8_t *buf, size_t len, size_t *hdr_len, size_t *msg_len);

#ifdef __cplusplus
}
#endif
