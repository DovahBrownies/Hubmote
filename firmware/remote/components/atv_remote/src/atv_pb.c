#include "atv_pb.h"

#include <string.h>

/* ---------------------------------------------------------------- writer */

void pb_writer_init(pb_writer_t *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = false;
}

size_t pb_varint_size(uint64_t v)
{
    size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        n++;
    }
    return n;
}

size_t pb_varint_encode(uint64_t v, uint8_t *out)
{
    size_t n = 0;
    while (v >= 0x80) {
        out[n++] = (uint8_t)(v | 0x80);
        v >>= 7;
    }
    out[n++] = (uint8_t)v;
    return n;
}

static void put_raw(pb_writer_t *w, const void *data, size_t len)
{
    if (w->overflow || len > w->cap - w->len) {
        w->overflow = true;
        return;
    }
    if (len) {
        memcpy(w->buf + w->len, data, len);
    }
    w->len += len;
}

void pb_put_varint(pb_writer_t *w, uint64_t v)
{
    uint8_t tmp[PB_VARINT_MAX];
    size_t n = pb_varint_encode(v, tmp);
    put_raw(w, tmp, n);
}

void pb_put_tag(pb_writer_t *w, uint32_t field, uint8_t wire_type)
{
    pb_put_varint(w, ((uint64_t)field << 3) | wire_type);
}

void pb_put_uint(pb_writer_t *w, uint32_t field, uint64_t v)
{
    pb_put_tag(w, field, PB_WT_VARINT);
    pb_put_varint(w, v);
}

void pb_put_int32(pb_writer_t *w, uint32_t field, int32_t v)
{
    /* int32 and enums are encoded as the 64-bit two's complement value. */
    pb_put_tag(w, field, PB_WT_VARINT);
    pb_put_varint(w, (uint64_t)(int64_t)v);
}

void pb_put_bytes(pb_writer_t *w, uint32_t field, const void *data, size_t len)
{
    pb_put_tag(w, field, PB_WT_LEN);
    pb_put_varint(w, len);
    put_raw(w, data, len);
}

void pb_put_string(pb_writer_t *w, uint32_t field, const char *s)
{
    pb_put_bytes(w, field, s ? s : "", s ? strlen(s) : 0);
}

void pb_put_int32_p3(pb_writer_t *w, uint32_t field, int32_t v)
{
    if (v != 0) {
        pb_put_int32(w, field, v);
    }
}

void pb_put_string_p3(pb_writer_t *w, uint32_t field, const char *s)
{
    if (s && s[0]) {
        pb_put_string(w, field, s);
    }
}

size_t pb_begin(pb_writer_t *w, uint32_t field)
{
    pb_put_tag(w, field, PB_WT_LEN);
    size_t marker = w->len;
    uint8_t zero = 0;
    put_raw(w, &zero, 1); /* length placeholder, patched by pb_end */
    return marker;
}

void pb_end(pb_writer_t *w, size_t marker)
{
    if (w->overflow) {
        return;
    }
    size_t body = w->len - marker - 1;
    size_t vlen = pb_varint_size(body);
    if (vlen > 1) {
        size_t extra = vlen - 1;
        if (extra > w->cap - w->len) {
            w->overflow = true;
            return;
        }
        memmove(w->buf + marker + vlen, w->buf + marker + 1, body);
        w->len += extra;
    }
    pb_varint_encode(body, w->buf + marker);
}

/* ---------------------------------------------------------------- reader */

void pb_reader_init(pb_reader_t *r, const uint8_t *buf, size_t len)
{
    r->p = buf;
    r->end = buf + len;
    r->error = false;
}

int pb_varint_decode(const uint8_t *buf, size_t len, uint64_t *out)
{
    uint64_t v = 0;
    for (size_t i = 0; i < PB_VARINT_MAX; i++) {
        if (i >= len) {
            return 0;
        }
        v |= (uint64_t)(buf[i] & 0x7F) << (7 * i);
        if (!(buf[i] & 0x80)) {
            *out = v;
            return (int)(i + 1);
        }
    }
    return -1;
}

bool pb_next(pb_reader_t *r, pb_field_t *f)
{
    if (r->error || r->p >= r->end) {
        return false;
    }
    uint64_t key;
    int n = pb_varint_decode(r->p, (size_t)(r->end - r->p), &key);
    if (n <= 0) {
        r->error = true;
        return false;
    }
    r->p += n;
    f->field = (uint32_t)(key >> 3);
    f->wire_type = (uint8_t)(key & 7);
    f->varint = 0;
    f->data = NULL;
    f->len = 0;
    if (f->field == 0 || (key >> 3) > 0x1FFFFFFF) {
        r->error = true;
        return false;
    }
    size_t left = (size_t)(r->end - r->p);
    switch (f->wire_type) {
    case PB_WT_VARINT:
        n = pb_varint_decode(r->p, left, &f->varint);
        if (n <= 0) {
            r->error = true;
            return false;
        }
        r->p += n;
        return true;
    case PB_WT_I64:
    case PB_WT_I32: {
        size_t sz = (f->wire_type == PB_WT_I64) ? 8 : 4;
        if (left < sz) {
            r->error = true;
            return false;
        }
        uint64_t v = 0;
        for (size_t i = 0; i < sz; i++) {
            v |= (uint64_t)r->p[i] << (8 * i);
        }
        f->varint = v;
        f->data = r->p;
        f->len = sz;
        r->p += sz;
        return true;
    }
    case PB_WT_LEN: {
        uint64_t len;
        n = pb_varint_decode(r->p, left, &len);
        if (n <= 0 || len > left - (size_t)n) {
            r->error = true;
            return false;
        }
        r->p += n;
        f->data = r->p;
        f->len = (size_t)len;
        r->p += len;
        return true;
    }
    default:
        /* Groups (3/4) are not used by these protocols. */
        r->error = true;
        return false;
    }
}

void pb_copy_string(const pb_field_t *f, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    size_t n = (f && f->wire_type == PB_WT_LEN) ? f->len : 0;
    if (n >= out_len) {
        n = out_len - 1;
        /* Do not cut a UTF-8 sequence in half: back off continuation bytes,
         * then drop the (now incomplete) lead byte. */
        if (n > 0 && n < f->len && (f->data[n] & 0xC0) == 0x80) {
            while (n > 0 && (f->data[n] & 0xC0) == 0x80) {
                n--;
            }
        }
    }
    if (n) {
        memcpy(out, f->data, n);
    }
    out[n] = '\0';
}

/* ----------------------------------------------------------------- frame */

pb_frame_result_t pb_frame_peek(const uint8_t *buf, size_t len, size_t *hdr_len, size_t *msg_len)
{
    uint64_t v = 0;
    int n = pb_varint_decode(buf, len, &v);
    *hdr_len = 0;
    *msg_len = 0;
    if (n < 0 || v > 0x7FFFFFFF) {
        return PB_FRAME_CORRUPT;
    }
    if (n == 0) {
        return PB_FRAME_NEED_MORE;
    }
    *hdr_len = (size_t)n;
    *msg_len = (size_t)v;
    return (len - (size_t)n >= (size_t)v) ? PB_FRAME_OK : PB_FRAME_NEED_MORE;
}
