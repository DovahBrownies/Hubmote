/*
 * Pure IR logic: no hardware, no ESP-IDF headers beyond esp_err.h. See
 * ir_codec.h. host_test/ compiles this file on a PC.
 */

#include "ir_codec.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void set_why(const char **why, const char *msg)
{
    if (why) {
        *why = msg;
    }
}

/* --- Codes ---------------------------------------------------------------- */

static bool carrier_ok(uint32_t hz)
{
    return hz == 0 || (hz >= IR_CARRIER_MIN_HZ && hz <= IR_CARRIER_MAX_HZ);
}

esp_err_t ir_codec_validate(const ir_code_t *code, const char **why)
{
    if (!code) {
        set_why(why, "no code");
        return ESP_ERR_INVALID_ARG;
    }
    if (!carrier_ok(code->carrier_hz)) {
        set_why(why, "carrier out of range (0 or 10000..500000 Hz)");
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t limit = 0; /* values must be below this; 0 = any 32 bits */
    switch (code->proto) {
    case IR_PROTO_NEC:
    case IR_PROTO_SAMSUNG32:
        return ESP_OK;
    case IR_PROTO_SIRC12:
        limit = 1u << 12;
        break;
    case IR_PROTO_SIRC15:
        limit = 1u << 15;
        break;
    case IR_PROTO_SIRC20:
        limit = 1u << 20;
        break;
    case IR_PROTO_RC5:
        limit = 1u << 12; /* 5-bit address, 7-bit command */
        break;
    case IR_PROTO_RC6:
        limit = 1u << 16;
        break;
    case IR_PROTO_RAW:
        if (code->count == 0) {
            set_why(why, "RAW code has no timings");
            return ESP_ERR_INVALID_ARG;
        }
        if (code->count > IR_MAX_TIMINGS) {
            set_why(why, "RAW code has more than IR_MAX_TIMINGS timings");
            return ESP_ERR_INVALID_SIZE;
        }
        for (size_t i = 0; i < code->count; i++) {
            if (code->timings[i] == 0) {
                set_why(why, "RAW timing of 0 us");
                return ESP_ERR_INVALID_ARG;
            }
        }
        return ESP_OK;
    default:
        set_why(why, "unknown protocol");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (code->value >= limit) {
        set_why(why, "value has more bits than the protocol");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static uint32_t ir_codec_default_carrier(ir_proto_t proto)
{
    switch (proto) {
    case IR_PROTO_SIRC12:
    case IR_PROTO_SIRC15:
    case IR_PROTO_SIRC20:
        return 40000;
    case IR_PROTO_RC5:
    case IR_PROTO_RC6:
        return 36000;
    default:
        return IR_DEFAULT_CARRIER_HZ;
    }
}

uint32_t ir_codec_carrier(const ir_code_t *code)
{
    return code->carrier_hz ? code->carrier_hz : ir_codec_default_carrier(code->proto);
}

static int sirc_bits(ir_proto_t proto)
{
    return proto == IR_PROTO_SIRC12 ? 12 : proto == IR_PROTO_SIRC15 ? 15 : 20;
}

/* Sony: leader, then the value's low bits LSB first, each a mark (one =
 * 1200 us, zero = 600 us) and a 600 us space; the last space is the gap. */
static size_t sirc_frame(int bits, uint32_t value, uint16_t *out, size_t max)
{
    size_t need = 2 + (size_t)bits * 2 - 1;
    if (max < need) {
        return 0;
    }
    size_t n = 0;
    out[n++] = IR_SIRC_LEAD_MARK;
    out[n++] = IR_SIRC_UNIT;
    for (int i = 0; i < bits; i++) {
        out[n++] = ((value >> i) & 1u) ? 2 * IR_SIRC_UNIT : IR_SIRC_UNIT;
        if (i < bits - 1) {
            out[n++] = IR_SIRC_UNIT;
        }
    }
    return n;
}

/* Builds timings from a sequence of half-bit levels (1 = carrier): runs of
 * the same level merge, idle before the first mark and after the last one
 * is dropped. */
typedef struct {
    uint16_t *out;
    size_t max;
    size_t n;
    int level; /* level of out[n - 1]; -1 before the first mark */
    bool overflow;
} halves_t;

static void put_run(halves_t *h, int level, uint32_t us)
{
    if (h->level < 0 && level == 0) {
        return; /* leading idle */
    }
    if (h->n > 0 && h->level == level) {
        h->out[h->n - 1] = (uint16_t)(h->out[h->n - 1] + us);
        return;
    }
    if (h->n == h->max) {
        h->overflow = true;
        return;
    }
    h->out[h->n++] = (uint16_t)us;
    h->level = level;
}

static size_t halves_end(halves_t *h)
{
    if (h->overflow) {
        return 0;
    }
    if (h->n > 0 && h->level == 0) {
        h->n--; /* trailing idle belongs to the gap */
    }
    return h->n;
}

/* Philips RC5: 14 Manchester bits of 1778 us, a one is idle then carrier.
 * Start bit, field bit (inverted command bit 6, so 1 for RC5), toggle,
 * 5 address bits and 6 command bits, MSB first. */
static size_t rc5_frame(uint32_t value, bool toggle, uint16_t *out, size_t max)
{
    uint32_t cmd = value & 0x7Fu;
    uint32_t addr = (value >> 7) & 0x1Fu;
    uint32_t bits = 1u << 13 | (cmd < 64 ? 1u : 0u) << 12 | (toggle ? 1u : 0u) << 11 | addr << 6 | (cmd & 0x3Fu);
    halves_t h = {.out = out, .max = max, .level = -1};
    for (int i = 13; i >= 0; i--) {
        int one = (bits >> i) & 1u;
        put_run(&h, one ? 0 : 1, IR_RC5_HALF);
        put_run(&h, one ? 1 : 0, IR_RC5_HALF);
    }
    return halves_end(&h);
}

/* RC6 mode 0: leader (6t carrier, 2t idle), start bit 1, mode 000, a
 * double-width toggle bit, then 8 address and 8 command bits MSB first. A
 * one is carrier then idle (the opposite of RC5). */
static void rc6_bit(halves_t *h, int one, uint32_t half)
{
    put_run(h, one ? 1 : 0, half);
    put_run(h, one ? 0 : 1, half);
}

static size_t rc6_frame(uint32_t value, bool toggle, uint16_t *out, size_t max)
{
    halves_t h = {.out = out, .max = max, .level = -1};
    put_run(&h, 1, 6 * IR_RC6_UNIT + 2); /* 2666 */
    put_run(&h, 0, 2 * IR_RC6_UNIT + 1); /* 889 */
    rc6_bit(&h, 1, IR_RC6_UNIT);
    for (int i = 0; i < 3; i++) {
        rc6_bit(&h, 0, IR_RC6_UNIT);
    }
    rc6_bit(&h, toggle, 2 * IR_RC6_UNIT + 1);
    for (int i = 15; i >= 0; i--) {
        rc6_bit(&h, (value >> i) & 1u, IR_RC6_UNIT);
    }
    return halves_end(&h);
}

static size_t proto32_frame(uint16_t lead_mark, uint16_t lead_space, uint32_t value,
                            uint16_t *out, size_t max)
{
    if (max < IR_PROTO32_TIMINGS) {
        return 0;
    }
    size_t n = 0;
    out[n++] = lead_mark;
    out[n++] = lead_space;
    for (int bit = 31; bit >= 0; bit--) {
        out[n++] = IR_BIT_MARK;
        out[n++] = ((value >> bit) & 1u) ? IR_ONE_SPACE : IR_ZERO_SPACE;
    }
    out[n++] = IR_BIT_MARK; /* stop */
    return n;
}

size_t ir_codec_frame(const ir_code_t *code, bool repeat, uint16_t *out, size_t max)
{
    return ir_codec_frame_toggle(code, repeat, false, out, max);
}

size_t ir_codec_frame_toggle(const ir_code_t *code, bool repeat, bool toggle, uint16_t *out, size_t max)
{
    if (ir_codec_validate(code, NULL) != ESP_OK || !out) {
        return 0;
    }
    switch (code->proto) {
    case IR_PROTO_SIRC12:
    case IR_PROTO_SIRC15:
    case IR_PROTO_SIRC20:
        return sirc_frame(sirc_bits(code->proto), code->value, out, max);
    case IR_PROTO_RC5:
        return rc5_frame(code->value, toggle, out, max);
    case IR_PROTO_RC6:
        return rc6_frame(code->value, toggle, out, max);
    case IR_PROTO_NEC:
        if (repeat) {
            if (max < IR_NEC_REPEAT_TIMINGS) {
                return 0;
            }
            out[0] = IR_NEC_LEAD_MARK;
            out[1] = IR_NEC_REPEAT_SPACE;
            out[2] = IR_BIT_MARK;
            return IR_NEC_REPEAT_TIMINGS;
        }
        return proto32_frame(IR_NEC_LEAD_MARK, IR_NEC_LEAD_SPACE, code->value, out, max);
    case IR_PROTO_SAMSUNG32:
        return proto32_frame(IR_SAMSUNG_LEAD_MARK, IR_SAMSUNG_LEAD_SPACE, code->value, out, max);
    case IR_PROTO_RAW:
        if (code->count > max) {
            return 0;
        }
        memcpy(out, code->timings, code->count * sizeof(uint16_t));
        return code->count;
    default:
        return 0;
    }
}

uint32_t ir_codec_sum_us(const uint16_t *t, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += t[i];
    }
    return sum;
}

uint32_t ir_codec_gap_us(ir_proto_t proto, uint32_t frame_us)
{
    uint32_t period = 0;
    uint32_t min_gap = IR_MIN_GAP_US;
    switch (proto) {
    case IR_PROTO_NEC:
    case IR_PROTO_SAMSUNG32:
        period = IR_FRAME_PERIOD_US;
        break;
    case IR_PROTO_SIRC12:
    case IR_PROTO_SIRC15:
    case IR_PROTO_SIRC20:
        period = IR_SIRC_PERIOD_US;
        min_gap = IR_SIRC_MIN_GAP_US;
        break;
    case IR_PROTO_RC5:
        period = IR_RC5_PERIOD_US;
        break;
    case IR_PROTO_RC6:
        period = IR_RC6_PERIOD_US;
        break;
    default:
        break;
    }
    if (period && frame_us + min_gap < period) {
        return period - frame_us;
    }
    return min_gap;
}

/* --- RMT symbols ------------------------------------------------------------ */

typedef struct {
    uint32_t *words;
    size_t max;
    size_t n;      /* complete words */
    bool half;     /* words[n] holds a first half */
    bool overflow;
} packer_t;

static void put_half(packer_t *p, uint32_t level, uint32_t ticks)
{
    if (p->overflow) {
        return;
    }
    if (!p->half) {
        if (p->n >= p->max) {
            p->overflow = true;
            return;
        }
        p->words[p->n] = ir_codec_word(ticks, level, 0, 0);
        p->half = true;
    } else {
        p->words[p->n] |= ir_codec_word(0, 0, ticks, level);
        p->n++;
        p->half = false;
    }
}

/* Splits us into the fewest halves of at most IR_SYM_MAX_TICKS, with an
 * even count when even is set. None of them is 0 (the end marker). */
static void put_level(packer_t *p, uint32_t level, uint32_t us, bool even)
{
    uint32_t k = (us + IR_SYM_MAX_TICKS - 1) / IR_SYM_MAX_TICKS;
    if (even && (k & 1u)) {
        k++;
    }
    if (us < k) {
        us = k; /* only reachable for a 1 us gap split in two */
    }
    for (uint32_t i = 0; i < k; i++) {
        put_half(p, level, us / k + (i < us % k ? 1u : 0u));
    }
}

size_t ir_codec_pack(const uint16_t *t, size_t n, uint32_t gap_us,
                     uint32_t *words, size_t max_words, size_t *frame_words)
{
    packer_t p = { .words = words, .max = max_words };
    for (size_t i = 0; i < n; i++) {
        if (t[i] == 0) {
            return 0; /* would break the mark/space alternation */
        }
        put_level(&p, (i & 1u) ? 0u : 1u, t[i], false);
    }
    if (p.half) {
        put_half(&p, 0, IR_SYM_PAD_US);
    }
    size_t frame = p.n;
    if (gap_us) {
        put_level(&p, 0, gap_us, true);
    }
    if (p.overflow || p.half) {
        return 0;
    }
    if (frame_words) {
        *frame_words = frame;
    }
    return p.n;
}

size_t ir_codec_unpack(const uint32_t *words, size_t n_words, uint16_t *out, size_t max,
                       bool *overflow)
{
    size_t n = 0;
    bool over = false;
    for (size_t i = 0; i < n_words && !over; i++) {
        for (int h = 0; h < 2; h++) {
            uint32_t w = h ? words[i] >> 16 : words[i];
            uint32_t d = w & 0x7FFFu;
            bool mark = ((w >> 15) & 1u) == 0; /* active-low receiver */
            if (d == 0) {
                goto end; /* end marker */
            }
            if (n == 0 && !mark) {
                continue; /* idle before the first mark */
            }
            bool last_is_mark = n > 0 && ((n - 1) & 1u) == 0;
            if (n > 0 && mark == last_is_mark) {
                uint32_t merged = (uint32_t)out[n - 1] + d;
                out[n - 1] = merged > UINT16_MAX ? UINT16_MAX : (uint16_t)merged;
                continue;
            }
            if (n >= max) {
                over = true;
                break;
            }
            out[n++] = (uint16_t)d;
        }
    }
end:
    if (n > 0 && (n & 1u) == 0) {
        n--; /* the idle after the last mark */
    }
    if (overflow) {
        *overflow = over;
    }
    return n;
}

/* --- Decoding and matching ------------------------------------------------ */

static bool near_pct(uint32_t v, uint32_t nominal, uint32_t pct)
{
    uint32_t tol = nominal * pct / 100;
    return v + tol >= nominal && v <= nominal + tol;
}

static bool bit_mark_ok(uint16_t v)
{
    return v >= IR_DEC_MARK_MIN && v <= IR_DEC_MARK_MAX;
}

bool ir_codec_is_nec_repeat(const uint16_t *t, size_t n)
{
    /* 3 timings, or 4 if a trailing space came along */
    if (n != IR_NEC_REPEAT_TIMINGS && n != IR_NEC_REPEAT_TIMINGS + 1) {
        return false;
    }
    return near_pct(t[0], IR_NEC_LEAD_MARK, IR_DEC_LEAD_PCT) &&
           near_pct(t[1], IR_NEC_REPEAT_SPACE, IR_DEC_LEAD_PCT) && bit_mark_ok(t[2]);
}

bool ir_codec_decode(const uint16_t *t, size_t n, ir_proto_t *proto, uint32_t *value)
{
    if (n != IR_PROTO32_TIMINGS && n != IR_PROTO32_TIMINGS + 1) {
        return false;
    }
    ir_proto_t p;
    if (near_pct(t[0], IR_NEC_LEAD_MARK, IR_DEC_LEAD_PCT) &&
        near_pct(t[1], IR_NEC_LEAD_SPACE, IR_DEC_LEAD_PCT)) {
        p = IR_PROTO_NEC;
    } else if (near_pct(t[0], IR_SAMSUNG_LEAD_MARK, IR_DEC_LEAD_PCT) &&
               near_pct(t[1], IR_SAMSUNG_LEAD_SPACE, IR_DEC_LEAD_PCT)) {
        p = IR_PROTO_SAMSUNG32;
    } else {
        return false;
    }
    uint32_t v = 0;
    for (size_t bit = 0; bit < 32; bit++) {
        uint16_t mark = t[2 + 2 * bit];
        uint16_t space = t[3 + 2 * bit];
        if (!bit_mark_ok(mark) || space < IR_DEC_SPACE_MIN || space > IR_DEC_SPACE_MAX) {
            return false;
        }
        v = (v << 1) | (space >= IR_DEC_BIT_SPLIT ? 1u : 0u);
    }
    if (!bit_mark_ok(t[IR_PROTO32_TIMINGS - 1])) {
        return false;
    }
    if (proto) {
        *proto = p;
    }
    if (value) {
        *value = v;
    }
    return true;
}

static uint32_t match_tol(uint32_t ref)
{
    uint32_t tol = ref * IR_MATCH_PCT / 100;
    return tol < IR_MATCH_MIN_US ? IR_MATCH_MIN_US : tol;
}

bool ir_codec_frames_match(const uint16_t *ref, size_t n_ref, const uint16_t *t, size_t n)
{
    if (n_ref < IR_MIN_FRAME_TIMINGS || n < IR_MIN_FRAME_TIMINGS) {
        return false;
    }
    size_t lo = n_ref < n ? n_ref : n;
    size_t hi = n_ref < n ? n : n_ref;
    if (hi - lo > 2) {
        return false;
    }
    if (hi > lo) {
        /* Only a trailing stop may differ: the longer frame's extra timings
         * must end in a short mark (even index = mark). */
        const uint16_t *longer = n_ref > n ? ref : t;
        if (((hi - 1) & 1u) != 0 || longer[hi - 1] > IR_MATCH_STOP_MAX_US) {
            return false;
        }
    }
    for (size_t i = 0; i < lo; i++) {
        uint32_t a = ref[i];
        uint32_t b = t[i];
        uint32_t diff = a > b ? a - b : b - a;
        if (diff > match_tol(a)) {
            return false;
        }
    }
    return true;
}

/* --- Learning state machine ------------------------------------------------- */

void ir_learner_init(ir_learner_t *l, int presses_needed)
{
    memset(l, 0, sizeof(*l));
    if (presses_needed < 1) {
        presses_needed = 1;
    }
    l->needed = (uint8_t)(presses_needed > 255 ? 255 : presses_needed);
}

static void learner_clear_avg(ir_learner_t *l)
{
    memset(l->sum, 0, sizeof(l->sum));
    memset(l->cnt, 0, sizeof(l->cnt));
    memset(l->avg, 0, sizeof(l->avg));
    l->len = 0;
    l->frames = 0;
}

static void learner_add(ir_learner_t *l, const uint16_t *t, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        l->sum[i] += t[i];
        l->cnt[i]++;
        l->avg[i] = (uint16_t)((l->sum[i] + l->cnt[i] / 2u) / l->cnt[i]);
    }
    if (n > l->len) {
        l->len = (uint16_t)n;
    }
    l->frames++;
}

static ir_feed_t learner_counted(const ir_learner_t *l)
{
    return l->frames >= l->needed ? IR_FEED_DONE : IR_FEED_PRESS;
}

ir_feed_t ir_learner_feed(ir_learner_t *l, const uint16_t *t, size_t n, int64_t end_us)
{
    if (ir_codec_is_nec_repeat(t, n)) {
        /* A held NEC button: keeps the current press going, never counts. */
        l->have_last = true;
        l->last_end_us = end_us;
        return IR_FEED_REPEAT;
    }
    if (n < IR_MIN_FRAME_TIMINGS || n > IR_MAX_TIMINGS) {
        return IR_FEED_NOISE;
    }

    /* Remotes resend the frame while the button is held (Samsung every
     * 108 ms, Sony at least 3 times per press). Only the first frame of a
     * press counts. */
    int64_t start_us = end_us - (int64_t)ir_codec_sum_us(t, n);
    bool same_press = l->have_last && start_us - l->last_end_us < IR_PRESS_GAP_US;
    l->have_last = true;
    l->last_end_us = end_us;
    if (same_press) {
        return IR_FEED_SAME_PRESS;
    }

    if (l->frames == 0 || ir_codec_frames_match(l->avg, l->len, t, n)) {
        learner_add(l, t, n);
        return learner_counted(l);
    }

    /* With one press so far, that press may have been the bad one: if this
     * frame matches the previous mismatch, start over from those two. */
    if (l->frames == 1 && l->alt_len && ir_codec_frames_match(l->alt, l->alt_len, t, n)) {
        learner_clear_avg(l);
        learner_add(l, l->alt, l->alt_len);
        learner_add(l, t, n);
        l->alt_len = 0;
        return learner_counted(l);
    }

    memcpy(l->alt, t, n * sizeof(uint16_t));
    l->alt_len = (uint16_t)n;
    return IR_FEED_MISMATCH;
}

int ir_learner_presses(const ir_learner_t *l)
{
    return l->frames;
}

void ir_learner_result(const ir_learner_t *l, ir_code_t *out)
{
    memset(out, 0, sizeof(*out));
    out->carrier_hz = IR_DEFAULT_CARRIER_HZ;
    ir_proto_t proto;
    uint32_t value;
    if (ir_codec_decode(l->avg, l->len, &proto, &value)) {
        out->proto = proto;
        out->value = value;
        return;
    }
    out->proto = IR_PROTO_RAW;
    out->count = l->len;
    memcpy(out->timings, l->avg, l->len * sizeof(uint16_t));
}

/* --- Text form ------------------------------------------------------------- */

static const char *proto_name(ir_proto_t proto)
{
    switch (proto) {
    case IR_PROTO_NEC:
        return "NEC";
    case IR_PROTO_SAMSUNG32:
        return "SAMSUNG32";
    case IR_PROTO_SIRC12:
        return "SIRC12";
    case IR_PROTO_SIRC15:
        return "SIRC15";
    case IR_PROTO_SIRC20:
        return "SIRC20";
    case IR_PROTO_RC5:
        return "RC5";
    case IR_PROTO_RC6:
        return "RC6";
    case IR_PROTO_RAW:
        return "RAW";
    default:
        return NULL;
    }
}

/* Protocols written as "NAME XXXXXXXX [carrier]". */
static const ir_proto_t k_value_protos[] = {
    IR_PROTO_NEC, IR_PROTO_SAMSUNG32, IR_PROTO_SIRC12, IR_PROTO_SIRC15, IR_PROTO_SIRC20, IR_PROTO_RC5, IR_PROTO_RC6,
};

typedef struct {
    char *buf;
    size_t len;
    size_t pos;
    bool overflow;
} writer_t;

static void put_text(writer_t *w, const char *s)
{
    size_t n = strlen(s);
    if (w->overflow || w->pos + n >= w->len) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->pos, s, n + 1);
    w->pos += n;
}

static void put_uint(writer_t *w, const char *prefix, uint32_t v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%s%" PRIu32, prefix, v);
    put_text(w, tmp);
}

esp_err_t ir_codec_format(const ir_code_t *code, char *buf, size_t len, const char **why)
{
    if (!buf || len == 0) {
        set_why(why, "no output buffer");
        return ESP_ERR_INVALID_ARG;
    }
    buf[0] = '\0';
    esp_err_t err = ir_codec_validate(code, why);
    if (err != ESP_OK) {
        return err;
    }

    writer_t w = { .buf = buf, .len = len };
    put_text(&w, proto_name(code->proto));
    if (code->proto == IR_PROTO_RAW) {
        put_uint(&w, " ", ir_codec_carrier(code));
        for (size_t i = 0; i < code->count; i++) {
            put_uint(&w, " ", code->timings[i]);
        }
    } else {
        char hex[16];
        snprintf(hex, sizeof(hex), " %08" PRIX32, code->value);
        put_text(&w, hex);
        if (code->carrier_hz && code->carrier_hz != ir_codec_default_carrier(code->proto)) {
            put_uint(&w, " ", code->carrier_hz);
        }
    }
    if (w.overflow) {
        buf[0] = '\0';
        set_why(why, "text buffer too small");
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Next whitespace-separated token; false at the end of the text. */
static bool next_token(const char *text, size_t *pos, size_t *start, size_t *len)
{
    size_t p = *pos;
    while (is_space(text[p])) {
        p++;
    }
    if (text[p] == '\0') {
        *pos = p;
        return false;
    }
    *start = p;
    while (text[p] != '\0' && !is_space(text[p])) {
        p++;
    }
    *len = p - *start;
    *pos = p;
    return true;
}

static bool token_is(const char *tok, size_t len, const char *name)
{
    if (strlen(name) != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = tok[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != name[i]) {
            return false;
        }
    }
    return true;
}

/* Plain decimal, digits only, at most 9 of them (fits in uint32_t). */
static bool parse_dec(const char *tok, size_t len, uint32_t *out)
{
    if (len == 0 || len > 9) {
        return false;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        if (tok[i] < '0' || tok[i] > '9') {
            return false;
        }
        v = v * 10u + (uint32_t)(tok[i] - '0');
    }
    *out = v;
    return true;
}

/* Exactly 8 hex digits, either case, no prefix. */
static bool parse_hex32(const char *tok, size_t len, uint32_t *out)
{
    if (len != 8) {
        return false;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        char c = tok[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else {
            return false;
        }
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

static esp_err_t parse_fail(ir_code_t *out, esp_err_t err, const char *msg, size_t at,
                            const char **why, size_t *where)
{
    memset(out, 0, sizeof(*out));
    set_why(why, msg);
    if (where) {
        *where = at;
    }
    return err;
}

esp_err_t ir_codec_parse(const char *text, ir_code_t *out, const char **why, size_t *where)
{
    if (!out) {
        set_why(why, "no output code");
        return ESP_ERR_INVALID_ARG;
    }
    if (!text) {
        return parse_fail(out, ESP_ERR_INVALID_ARG, "no text", 0, why, where);
    }
    memset(out, 0, sizeof(*out));

    size_t pos = 0, start = 0, len = 0;
    if (!next_token(text, &pos, &start, &len)) {
        return parse_fail(out, ESP_ERR_INVALID_ARG, "empty code text", pos, why, where);
    }

    int value_proto = -1;
    for (size_t i = 0; i < sizeof(k_value_protos) / sizeof(k_value_protos[0]); i++) {
        if (token_is(text + start, len, proto_name(k_value_protos[i]))) {
            value_proto = (int)k_value_protos[i];
        }
    }
    if (value_proto >= 0) {
        out->proto = (ir_proto_t)value_proto;
        if (!next_token(text, &pos, &start, &len)) {
            return parse_fail(out, ESP_ERR_INVALID_ARG, "missing 8-digit hex value", pos, why,
                              where);
        }
        if (!parse_hex32(text + start, len, &out->value)) {
            return parse_fail(out, ESP_ERR_INVALID_ARG, "value must be exactly 8 hex digits",
                              start, why, where);
        }
        if (ir_codec_validate(out, NULL) != ESP_OK) {
            return parse_fail(out, ESP_ERR_INVALID_ARG, "value has more bits than the protocol", start,
                              why, where);
        }
        if (next_token(text, &pos, &start, &len)) {
            uint32_t hz;
            if (!parse_dec(text + start, len, &hz) || hz < IR_CARRIER_MIN_HZ ||
                hz > IR_CARRIER_MAX_HZ) {
                return parse_fail(out, ESP_ERR_INVALID_ARG,
                                  "optional carrier must be 10000..500000 Hz", start, why, where);
            }
            out->carrier_hz = hz;
            if (next_token(text, &pos, &start, &len)) {
                return parse_fail(out, ESP_ERR_INVALID_ARG, "unexpected text after the code",
                                  start, why, where);
            }
        }
        return ESP_OK;
    }

    if (token_is(text + start, len, "RAW")) {
        out->proto = IR_PROTO_RAW;
        if (!next_token(text, &pos, &start, &len)) {
            return parse_fail(out, ESP_ERR_INVALID_ARG, "missing carrier", pos, why, where);
        }
        uint32_t hz;
        if (!parse_dec(text + start, len, &hz) || !carrier_ok(hz)) {
            return parse_fail(out, ESP_ERR_INVALID_ARG, "carrier must be 0 or 10000..500000 Hz",
                              start, why, where);
        }
        out->carrier_hz = hz;
        size_t n = 0;
        while (next_token(text, &pos, &start, &len)) {
            uint32_t us;
            if (n == IR_MAX_TIMINGS) {
                return parse_fail(out, ESP_ERR_INVALID_SIZE, "more than 512 timings", start, why,
                                  where);
            }
            if (!parse_dec(text + start, len, &us) || us == 0 || us > UINT16_MAX) {
                return parse_fail(out, ESP_ERR_INVALID_ARG, "timing must be 1..65535 us", start,
                                  why, where);
            }
            out->timings[n++] = (uint16_t)us;
        }
        if (n == 0) {
            return parse_fail(out, ESP_ERR_INVALID_ARG, "RAW needs at least one timing", pos, why,
                              where);
        }
        out->count = (uint16_t)n;
        return ESP_OK;
    }

    return parse_fail(out, ESP_ERR_NOT_SUPPORTED,
                      "unknown protocol (NEC, SAMSUNG32, SIRC12/15/20, RC5, RC6 or RAW)", start, why, where);
}
