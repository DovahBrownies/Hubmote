/*
 * Host tests for ir_codec.c, the pure IR logic (no hardware, no ESP-IDF).
 *
 * Run host_test\run_host_tests.ps1, or from the ir component directory:
 *   gcc -std=c11 -Wall -Wextra -Werror -O1 -Ihost_test/stub -Iinclude -I. \
 *       host_test/test_ir_codec.c ir_codec.c -o ir_codec_test.exe
 *   ./ir_codec_test.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir_codec.h"

static int s_checks;
static int s_fails;

#define CHECK(cond)                                                              \
    do {                                                                         \
        s_checks++;                                                              \
        if (!(cond)) {                                                           \
            s_fails++;                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long long a_ = (long long)(a), b_ = (long long)(b);                      \
        s_checks++;                                                              \
        if (a_ != b_) {                                                          \
            s_fails++;                                                           \
            printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, \
                   #a, #b, a_, b_);                                              \
        }                                                                        \
    } while (0)

#define CHECK_STR(a, b)                                                          \
    do {                                                                         \
        s_checks++;                                                              \
        if (strcmp((a), (b)) != 0) {                                             \
            s_fails++;                                                           \
            printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a),    \
                   (b));                                                         \
        }                                                                        \
    } while (0)

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* Deterministic PRNG so failures reproduce. */
static uint32_t s_rng = 0x1234567u;

static uint32_t rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng;
}

static uint32_t rnd32(void)
{
    return (rnd() >> 16) | (rnd() & 0xFFFF0000u);
}

/* Uniform in [v - pct%, v + pct%]. */
static uint16_t jitter(uint16_t v, int pct)
{
    int span = v * pct / 100;
    int d = (int)((rnd() >> 8) % (uint32_t)(2 * span + 1)) - span;
    return (uint16_t)(v + d);
}

static void jitter_all(uint16_t *t, size_t n, int pct)
{
    for (size_t i = 0; i < n; i++) {
        t[i] = jitter(t[i], pct);
    }
}

static void scale_all(uint16_t *t, size_t n, int percent)
{
    for (size_t i = 0; i < n; i++) {
        t[i] = (uint16_t)((uint32_t)t[i] * (uint32_t)percent / 100u);
    }
}

/* Builds what the RMT RX channel would hand over for timings t from an
 * active-low receiver: optional idle before the first mark, levels
 * inverted (mark = 0), optionally a level split in two halves, ending with
 * a zero-duration end marker. */
static size_t make_rx_words(const uint16_t *t, size_t n, uint32_t *w, size_t max, bool lead_idle,
                            bool split)
{
    static uint32_t lv[4 * IR_MAX_TIMINGS], du[4 * IR_MAX_TIMINGS];
    size_t h = 0;
    if (lead_idle) {
        lv[h] = 1;
        du[h] = 700;
        h++;
    }
    for (size_t i = 0; i < n; i++) {
        uint32_t level = (i & 1u) ? 1u : 0u;
        if (split && t[i] > 1000 && (rnd() & 0x100u)) {
            lv[h] = level;
            du[h] = t[i] / 2u;
            h++;
            lv[h] = level;
            du[h] = t[i] - t[i] / 2u;
            h++;
        } else {
            lv[h] = level;
            du[h] = t[i];
            h++;
        }
    }
    lv[h] = 1;
    du[h] = 0; /* end marker */
    h++;
    if (h & 1u) {
        lv[h] = 0;
        du[h] = 0;
        h++;
    }
    size_t nw = h / 2;
    if (nw > max) {
        return 0;
    }
    for (size_t i = 0; i < nw; i++) {
        w[i] = ir_codec_word(du[2 * i], lv[2 * i], du[2 * i + 1], lv[2 * i + 1]);
    }
    return nw;
}

static ir_code_t s_code, s_code2;
static ir_learner_t s_learner;
static uint16_t s_t[IR_MAX_TIMINGS + 8];
static uint16_t s_t2[IR_MAX_TIMINGS + 8];
static uint32_t s_words[IR_CODEC_MAX_WORDS + 8];
static uint32_t s_words2[IR_CODEC_MAX_WORDS + 8];
static char s_text[IR_CODE_TEXT_MAX];

static void set_proto32(ir_code_t *c, ir_proto_t proto, uint32_t value)
{
    memset(c, 0, sizeof(*c));
    c->proto = proto;
    c->value = value;
}

/* ------------------------------------------------------------------------ */

static void test_samsung_power_exact(void)
{
    /* Samsung TV power toggle, written out bit by bit from the standard
     * timing: 4500/4500 leader, 560 mark, 560 (0) or 1690 (1) space, stop. */
    static const uint16_t expected[67] = {
        4500, 4500,
        /* E0 = 1110 0000 */
        560, 1690, 560, 1690, 560, 1690, 560, 560, 560, 560, 560, 560, 560, 560, 560, 560,
        /* E0 = 1110 0000 */
        560, 1690, 560, 1690, 560, 1690, 560, 560, 560, 560, 560, 560, 560, 560, 560, 560,
        /* 40 = 0100 0000 */
        560, 560, 560, 1690, 560, 560, 560, 560, 560, 560, 560, 560, 560, 560, 560, 560,
        /* BF = 1011 1111 */
        560, 1690, 560, 560, 560, 1690, 560, 1690, 560, 1690, 560, 1690, 560, 1690, 560, 1690,
        560, /* stop */
    };
    set_proto32(&s_code, IR_PROTO_SAMSUNG32, 0xE0E040BFu);
    size_t n = ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS);
    CHECK_EQ(n, 67);
    CHECK(memcmp(s_t, expected, sizeof(expected)) == 0);
    /* SAMSUNG32 repeats are full frames */
    CHECK_EQ(ir_codec_frame(&s_code, true, s_t2, IR_MAX_TIMINGS), 67);
    CHECK(memcmp(s_t2, expected, sizeof(expected)) == 0);

    uint32_t frame_us = ir_codec_sum_us(s_t, n);
    CHECK_EQ(frame_us, 61220);
    uint32_t gap = ir_codec_gap_us(IR_PROTO_SAMSUNG32, frame_us);
    CHECK_EQ(gap, 108000 - 61220);

    /* Packed for the RMT: mark = level 1, 67 halves + a 100 us pad = 34
     * words, then the 46780 us gap as one word of two halves. */
    size_t frame_words = 0;
    size_t nw = ir_codec_pack(s_t, n, gap, s_words, IR_CODEC_MAX_WORDS, &frame_words);
    CHECK_EQ(frame_words, 34);
    CHECK_EQ(nw, 35);
    CHECK_EQ(s_words[0], ir_codec_word(4500, 1, 4500, 0));
    CHECK_EQ(s_words[1], ir_codec_word(560, 1, 1690, 0));
    CHECK_EQ(s_words[33], ir_codec_word(560, 1, IR_SYM_PAD_US, 0));
    CHECK_EQ(s_words[34], ir_codec_word(23390, 0, 23390, 0));

    ir_proto_t proto = IR_PROTO_RAW;
    uint32_t value = 0;
    CHECK(ir_codec_decode(expected, 67, &proto, &value));
    CHECK_EQ(proto, IR_PROTO_SAMSUNG32);
    CHECK_EQ(value, 0xE0E040BFu);
}

static void test_nec_frame_and_repeat(void)
{
    set_proto32(&s_code, IR_PROTO_NEC, 0x20DF10EFu);
    size_t n = ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS);
    CHECK_EQ(n, 67);
    CHECK_EQ(s_t[0], 9000);
    CHECK_EQ(s_t[1], 4500);
    /* 0x20 = 0010 0000: third bit is the first 1 */
    CHECK_EQ(s_t[3], 560);
    CHECK_EQ(s_t[5], 560);
    CHECK_EQ(s_t[7], 1690);
    CHECK_EQ(s_t[66], 560);
    uint32_t frame_us = ir_codec_sum_us(s_t, n);
    CHECK_EQ(frame_us + ir_codec_gap_us(IR_PROTO_NEC, frame_us), 108000);

    /* Repeat code: 9000 / 2250 / 560 every 108 ms */
    size_t rn = ir_codec_frame(&s_code, true, s_t2, IR_MAX_TIMINGS);
    CHECK_EQ(rn, 3);
    CHECK_EQ(s_t2[0], 9000);
    CHECK_EQ(s_t2[1], 2250);
    CHECK_EQ(s_t2[2], 560);
    uint32_t rep_us = ir_codec_sum_us(s_t2, rn);
    CHECK_EQ(rep_us, 11810);
    CHECK_EQ(rep_us + ir_codec_gap_us(IR_PROTO_NEC, rep_us), 108000);
    CHECK(ir_codec_is_nec_repeat(s_t2, rn));
    CHECK(!ir_codec_decode(s_t2, rn, NULL, NULL));
    CHECK(!ir_codec_is_nec_repeat(s_t, n));

    /* Repeat codes with jitter, through the RX path */
    for (int i = 0; i < 200; i++) {
        uint16_t r[3] = { 9000, 2250, 560 };
        jitter_all(r, 3, 20);
        size_t nw = make_rx_words(r, 3, s_words, IR_CODEC_MAX_WORDS, i & 1, true);
        bool over = true;
        size_t un = ir_codec_unpack(s_words, nw, s_t2, IR_MAX_TIMINGS, &over);
        CHECK(!over);
        CHECK_EQ(un, 3);
        CHECK(ir_codec_is_nec_repeat(s_t2, un));
    }
    /* Not a repeat: Samsung-length leader, or missing mark */
    uint16_t notrep[3] = { 4500, 2250, 560 };
    CHECK(!ir_codec_is_nec_repeat(notrep, 3));
    CHECK(!ir_codec_is_nec_repeat(s_t2, 2));

    /* RAW gap is a flat 40 ms */
    CHECK_EQ(ir_codec_gap_us(IR_PROTO_RAW, 1000), 40000);
    /* Long protocol frames still get 40 ms */
    CHECK_EQ(ir_codec_gap_us(IR_PROTO_NEC, 80000), 40000);
}

static void roundtrip_one(ir_proto_t proto, uint32_t value, int pct, int fixed_scale)
{
    set_proto32(&s_code, proto, value);
    size_t n = ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS);
    CHECK_EQ(n, 67);
    if (fixed_scale) {
        scale_all(s_t, n, fixed_scale);
    } else {
        jitter_all(s_t, n, pct);
    }
    /* Straight decode */
    ir_proto_t p = IR_PROTO_RAW;
    uint32_t v = 0;
    bool ok = ir_codec_decode(s_t, n, &p, &v);
    CHECK(ok);
    if (ok) {
        CHECK_EQ(p, proto);
        CHECK_EQ(v, value);
    }
    /* Through RMT RX symbols from an active-low receiver */
    size_t nw = make_rx_words(s_t, n, s_words, IR_CODEC_MAX_WORDS, rnd() & 0x100u, true);
    CHECK(nw > 0);
    bool over = true;
    size_t un = ir_codec_unpack(s_words, nw, s_t2, IR_MAX_TIMINGS, &over);
    CHECK(!over);
    CHECK_EQ(un, n);
    CHECK(memcmp(s_t, s_t2, n * sizeof(uint16_t)) == 0);
    ok = ir_codec_decode(s_t2, un, &p, &v);
    CHECK(ok);
    if (ok) {
        CHECK_EQ(p, proto);
        CHECK_EQ(v, value);
    }
}

static void test_roundtrip_jitter(void)
{
    static const uint32_t fixed_values[] = { 0x00000000u, 0xFFFFFFFFu, 0xE0E040BFu,
                                             0x20DF10EFu, 0x80000001u, 0x55AA55AAu };
    for (size_t i = 0; i < COUNT_OF(fixed_values); i++) {
        for (int proto = IR_PROTO_NEC; proto <= IR_PROTO_SAMSUNG32; proto++) {
            roundtrip_one((ir_proto_t)proto, fixed_values[i], 0, 120); /* all +20% */
            roundtrip_one((ir_proto_t)proto, fixed_values[i], 0, 80);  /* all -20% */
            roundtrip_one((ir_proto_t)proto, fixed_values[i], 20, 0);
        }
    }
    for (int i = 0; i < 2000; i++) {
        roundtrip_one((i & 1) ? IR_PROTO_SAMSUNG32 : IR_PROTO_NEC, rnd32(), 20, 0);
    }

    /* Things that must not decode */
    set_proto32(&s_code, IR_PROTO_NEC, 0x12345678u);
    size_t n = ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS);
    memcpy(s_t2, s_t, n * sizeof(uint16_t));
    s_t2[0] = 6000; /* leader between the two protocols */
    CHECK(!ir_codec_decode(s_t2, n, NULL, NULL));
    memcpy(s_t2, s_t, n * sizeof(uint16_t));
    s_t2[10] = 3000; /* space too long for a bit */
    CHECK(!ir_codec_decode(s_t2, n, NULL, NULL));
    memcpy(s_t2, s_t, n * sizeof(uint16_t));
    s_t2[66] = 2000; /* stop mark too long */
    CHECK(!ir_codec_decode(s_t2, n, NULL, NULL));
    CHECK(!ir_codec_decode(s_t, n - 2, NULL, NULL)); /* 31 bits */
    /* A trailing space (e.g. from RAW text) is fine */
    s_t[n] = 40000;
    ir_proto_t p;
    uint32_t v;
    CHECK(ir_codec_decode(s_t, n + 1, &p, &v));
    CHECK_EQ(v, 0x12345678u);
}

static void test_pack(void)
{
    size_t fw = 99;
    /* Odd halves get a pad */
    uint16_t a[] = { 100, 200, 300 };
    CHECK_EQ(ir_codec_pack(a, 3, 0, s_words, 8, &fw), 2);
    CHECK_EQ(fw, 2);
    CHECK_EQ(s_words[0], ir_codec_word(100, 1, 200, 0));
    CHECK_EQ(s_words[1], ir_codec_word(300, 1, IR_SYM_PAD_US, 0));

    /* Long levels are split into the fewest halves of <= 32767 ticks */
    uint16_t b[] = { 40000 };
    CHECK_EQ(ir_codec_pack(b, 1, 0, s_words, 8, &fw), 1);
    CHECK_EQ(s_words[0], ir_codec_word(20000, 1, 20000, 1));
    uint16_t c[] = { 65535 };
    CHECK_EQ(ir_codec_pack(c, 1, 0, s_words, 8, &fw), 2);
    CHECK_EQ(s_words[0], ir_codec_word(21845, 1, 21845, 1));
    CHECK_EQ(s_words[1], ir_codec_word(21845, 1, IR_SYM_PAD_US, 0));
    uint16_t d[] = { 32767, 32768 };
    CHECK_EQ(ir_codec_pack(d, 2, 0, s_words, 8, &fw), 2);
    CHECK_EQ(s_words[0], ir_codec_word(32767, 1, 16384, 0));
    CHECK_EQ(s_words[1], ir_codec_word(16384, 0, IR_SYM_PAD_US, 0));

    /* The gap is whole words, so the frame alone is a prefix */
    uint16_t e[] = { 9000, 4500, 560, 560, 560 };
    size_t total = ir_codec_pack(e, 5, 96190, s_words, 16, &fw);
    CHECK_EQ(fw, 3);
    CHECK_EQ(total, 5);
    CHECK_EQ(s_words[3], ir_codec_word(24048, 0, 24048, 0));
    CHECK_EQ(s_words[4], ir_codec_word(24047, 0, 24047, 0));
    size_t fw2 = 0;
    CHECK_EQ(ir_codec_pack(e, 5, 0, s_words2, 16, &fw2), 3);
    CHECK(memcmp(s_words, s_words2, 3 * sizeof(uint32_t)) == 0);
    /* Tiny gap never produces a zero (end marker) half */
    CHECK_EQ(ir_codec_pack(e, 5, 1, s_words, 16, &fw), 4);
    CHECK_EQ(s_words[3], ir_codec_word(1, 0, 1, 0));

    /* No zero durations anywhere in a worst-case RAW frame */
    for (size_t i = 0; i < IR_MAX_TIMINGS; i++) {
        s_t[i] = 65535;
    }
    total = ir_codec_pack(s_t, IR_MAX_TIMINGS, 108000, s_words, IR_CODEC_MAX_WORDS, &fw);
    CHECK(total > 0 && total <= IR_CODEC_MAX_WORDS);
    bool zero = false;
    for (size_t i = 0; i < total; i++) {
        if ((s_words[i] & 0x7FFFu) == 0 || ((s_words[i] >> 16) & 0x7FFFu) == 0) {
            zero = true;
        }
    }
    CHECK(!zero);

    /* Overflow and invalid input */
    CHECK_EQ(ir_codec_pack(e, 5, 96190, s_words, 4, &fw), 0);
    uint16_t z[] = { 100, 0, 100 };
    CHECK_EQ(ir_codec_pack(z, 3, 0, s_words, 8, &fw), 0);
}

static void test_unpack(void)
{
    bool over = true;
    /* idle, mark 9000 (split), space 4500, mark 560, end */
    uint32_t w[] = {
        ir_codec_word(700, 1, 5000, 0),
        ir_codec_word(4000, 0, 4500, 1),
        ir_codec_word(560, 0, 0, 1),
    };
    size_t n = ir_codec_unpack(w, 3, s_t, IR_MAX_TIMINGS, &over);
    CHECK(!over);
    CHECK_EQ(n, 3);
    CHECK_EQ(s_t[0], 9000);
    CHECK_EQ(s_t[1], 4500);
    CHECK_EQ(s_t[2], 560);

    /* Trailing idle with a real duration is dropped; data after the end
     * marker is ignored */
    uint32_t w2[] = {
        ir_codec_word(560, 0, 560, 1),
        ir_codec_word(560, 0, 12000, 1),
        ir_codec_word(0, 1, 0, 0),
        ir_codec_word(999, 0, 999, 1),
    };
    n = ir_codec_unpack(w2, 4, s_t, IR_MAX_TIMINGS, &over);
    CHECK_EQ(n, 3);

    /* Overflow */
    uint32_t w3[] = {
        ir_codec_word(500, 0, 500, 1),
        ir_codec_word(500, 0, 500, 1),
        ir_codec_word(500, 0, 0, 1),
    };
    n = ir_codec_unpack(w3, 3, s_t, 3, &over);
    CHECK(over);
    CHECK_EQ(n, 3);
    n = ir_codec_unpack(w3, 3, s_t, 5, &over);
    CHECK(!over);
    CHECK_EQ(n, 5);

    /* Nothing but idle */
    uint32_t w4[] = { ir_codec_word(3000, 1, 0, 1) };
    CHECK_EQ(ir_codec_unpack(w4, 1, s_t, 8, &over), 0);
}

static void test_match(void)
{
    uint16_t ref[] = { 1000, 2000, 400, 400, 1000, 600, 560 };
    uint16_t t[9];
    memcpy(t, ref, sizeof(ref));
    CHECK(ir_codec_frames_match(ref, 7, t, 7));
    t[0] = 1250; /* +25% */
    CHECK(ir_codec_frames_match(ref, 7, t, 7));
    t[0] = 1251;
    CHECK(!ir_codec_frames_match(ref, 7, t, 7));
    t[0] = 750; /* -25% */
    CHECK(ir_codec_frames_match(ref, 7, t, 7));
    t[0] = 749;
    CHECK(!ir_codec_frames_match(ref, 7, t, 7));
    memcpy(t, ref, sizeof(ref));
    t[2] = 550; /* short timing: +-150 us beats 25% */
    CHECK(ir_codec_frames_match(ref, 7, t, 7));
    t[2] = 551;
    CHECK(!ir_codec_frames_match(ref, 7, t, 7));
    t[2] = 250;
    CHECK(ir_codec_frames_match(ref, 7, t, 7));
    t[2] = 249;
    CHECK(!ir_codec_frames_match(ref, 7, t, 7));

    /* Trailing stop allowed, either way round */
    memcpy(t, ref, sizeof(ref));
    t[7] = 1000;
    t[8] = 560;
    CHECK(ir_codec_frames_match(ref, 7, t, 9));
    CHECK(ir_codec_frames_match(t, 9, ref, 7));
    t[8] = 2000; /* not a stop */
    CHECK(!ir_codec_frames_match(ref, 7, t, 9));
    CHECK(!ir_codec_frames_match(ref, 7, t, 8)); /* ends in a space */
    uint16_t big[11] = { 1000, 2000, 400, 400, 1000, 600, 560, 500, 500, 500, 500 };
    CHECK(!ir_codec_frames_match(ref, 7, big, 11));
    CHECK(!ir_codec_frames_match(ref, 5, t, 5)); /* below the noise floor */

    /* Jittered NEC frames match each other at +-10% */
    set_proto32(&s_code, IR_PROTO_NEC, 0x20DF10EFu);
    size_t n = ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS);
    for (int i = 0; i < 200; i++) {
        memcpy(s_t2, s_t, n * sizeof(uint16_t));
        jitter_all(s_t2, n, 10);
        CHECK(ir_codec_frames_match(s_t, n, s_t2, n));
    }
    /* A different NEC code doesn't */
    set_proto32(&s_code2, IR_PROTO_NEC, 0x20DF40BFu);
    ir_codec_frame(&s_code2, false, s_t2, IR_MAX_TIMINGS);
    CHECK(!ir_codec_frames_match(s_t, n, s_t2, n));
}

static size_t nec_frame(uint32_t value, uint16_t *out, int pct)
{
    ir_code_t c;
    set_proto32(&c, IR_PROTO_NEC, value);
    size_t n = ir_codec_frame(&c, false, out, IR_MAX_TIMINGS);
    jitter_all(out, n, pct);
    return n;
}

static void test_learner_basic(void)
{
    ir_learner_init(&s_learner, 3);
    int64_t t = 1000000;
    size_t n = nec_frame(0x20DF10EFu, s_t, 10);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t), IR_FEED_PRESS);
    CHECK_EQ(ir_learner_presses(&s_learner), 1);

    /* Holding the button: NEC repeat codes every 108 ms are ignored and
     * don't count */
    for (int i = 1; i <= 5; i++) {
        uint16_t r[3] = { 9000, 2250, 560 };
        jitter_all(r, 3, 10);
        CHECK_EQ(ir_learner_feed(&s_learner, r, 3, t + i * 108000), IR_FEED_REPEAT);
    }
    CHECK_EQ(ir_learner_presses(&s_learner), 1);
    t += 5 * 108000;

    /* A frame right after the repeats is still the same press */
    n = nec_frame(0x20DF10EFu, s_t, 10);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t + 108000), IR_FEED_SAME_PRESS);
    t += 108000;

    /* A real second press, then noise, then the third press */
    t += 600000;
    n = nec_frame(0x20DF10EFu, s_t, 10);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t), IR_FEED_PRESS);
    CHECK_EQ(ir_learner_presses(&s_learner), 2);
    uint16_t noise[2] = { 300, 300 };
    CHECK_EQ(ir_learner_feed(&s_learner, noise, 2, t + 10000), IR_FEED_NOISE);
    t += 700000;
    n = nec_frame(0x20DF10EFu, s_t, 10);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t), IR_FEED_DONE);
    CHECK_EQ(ir_learner_presses(&s_learner), 3);

    ir_learner_result(&s_learner, &s_code);
    CHECK_EQ(s_code.proto, IR_PROTO_NEC);
    CHECK_EQ(s_code.value, 0x20DF10EFu);
    CHECK_EQ(s_code.carrier_hz, 38000);
    CHECK_EQ(s_code.count, 0);
}

static void test_learner_samsung_held(void)
{
    /* Samsung resends the full frame every 108 ms while held */
    ir_learner_init(&s_learner, 2);
    ir_code_t c;
    set_proto32(&c, IR_PROTO_SAMSUNG32, 0xE0E040BFu);
    size_t n = ir_codec_frame(&c, false, s_t, IR_MAX_TIMINGS);
    uint32_t frame_us = ir_codec_sum_us(s_t, n);
    int64_t start = 5000000;
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, start + frame_us), IR_FEED_PRESS);
    for (int i = 1; i < 4; i++) {
        CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, start + i * 108000 + frame_us),
                 IR_FEED_SAME_PRESS);
    }
    start += 3 * 108000 + 400000;
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, start + frame_us), IR_FEED_DONE);
    ir_learner_result(&s_learner, &s_code);
    CHECK_EQ(s_code.proto, IR_PROTO_SAMSUNG32);
    CHECK_EQ(s_code.value, 0xE0E040BFu);
}

static void test_learner_mismatch(void)
{
    ir_learner_init(&s_learner, 3);
    int64_t t = 0;
    size_t n = nec_frame(0x20DF10EFu, s_t, 5);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t += 1000000), IR_FEED_PRESS);
    n = nec_frame(0x20DF40BFu, s_t, 5); /* a different button */
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t += 1000000), IR_FEED_MISMATCH);
    CHECK_EQ(ir_learner_presses(&s_learner), 1); /* the first press is kept */
    n = nec_frame(0x20DF10EFu, s_t, 5);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t += 1000000), IR_FEED_PRESS);
    CHECK_EQ(ir_learner_presses(&s_learner), 2);
    n = nec_frame(0x20DF40BFu, s_t, 5); /* with 2 presses, no swap */
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t += 1000000), IR_FEED_MISMATCH);
    n = nec_frame(0x20DF40BFu, s_t, 5);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t += 1000000), IR_FEED_MISMATCH);
    CHECK_EQ(ir_learner_presses(&s_learner), 2);
    n = nec_frame(0x20DF10EFu, s_t, 5);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t += 1000000), IR_FEED_DONE);
    ir_learner_result(&s_learner, &s_code);
    CHECK_EQ(s_code.proto, IR_PROTO_NEC);
    CHECK_EQ(s_code.value, 0x20DF10EFu);

    /* A bad first press: the next two agree with each other, so they win */
    ir_learner_init(&s_learner, 2);
    uint16_t junk[9] = { 3000, 700, 800, 700, 2500, 900, 600, 1200, 700 };
    CHECK_EQ(ir_learner_feed(&s_learner, junk, 9, t += 1000000), IR_FEED_PRESS);
    n = nec_frame(0xE0E040BFu, s_t, 5);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t += 1000000), IR_FEED_MISMATCH);
    n = nec_frame(0xE0E040BFu, s_t, 5);
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, n, t += 1000000), IR_FEED_DONE);
    CHECK_EQ(ir_learner_presses(&s_learner), 2);
    ir_learner_result(&s_learner, &s_code);
    CHECK_EQ(s_code.proto, IR_PROTO_NEC);
    CHECK_EQ(s_code.value, 0xE0E040BFu);
}

static void test_learner_raw_average(void)
{
    /* Not NEC / Samsung: comes back RAW, averaged */
    static const uint16_t base[7] = { 2400, 600, 1200, 600, 600, 600, 1200 };
    static const int scale[3] = { 90, 100, 110 };
    ir_learner_init(&s_learner, 3);
    int64_t t = 0;
    for (int p = 0; p < 3; p++) {
        uint16_t f[7];
        memcpy(f, base, sizeof(base));
        scale_all(f, 7, scale[p]);
        ir_feed_t r = ir_learner_feed(&s_learner, f, 7, t += 500000);
        CHECK_EQ(r, p == 2 ? IR_FEED_DONE : IR_FEED_PRESS);
    }
    ir_learner_result(&s_learner, &s_code);
    CHECK_EQ(s_code.proto, IR_PROTO_RAW);
    CHECK_EQ(s_code.carrier_hz, 38000);
    CHECK_EQ(s_code.count, 7);
    CHECK(memcmp(s_code.timings, base, sizeof(base)) == 0);

    /* Trailing stop on only some frames: kept, averaged over those */
    ir_learner_init(&s_learner, 3);
    uint16_t a[9] = { 2400, 600, 1200, 600, 600, 600, 1200, 1000, 500 };
    uint16_t b[9] = { 2400, 600, 1200, 600, 600, 600, 1200, 1100, 600 };
    CHECK_EQ(ir_learner_feed(&s_learner, a, 7, t += 500000), IR_FEED_PRESS);
    CHECK_EQ(ir_learner_feed(&s_learner, a, 9, t += 500000), IR_FEED_PRESS);
    CHECK_EQ(ir_learner_feed(&s_learner, b, 9, t += 500000), IR_FEED_DONE);
    ir_learner_result(&s_learner, &s_code);
    CHECK_EQ(s_code.proto, IR_PROTO_RAW);
    CHECK_EQ(s_code.count, 9);
    CHECK_EQ(s_code.timings[7], 1050);
    CHECK_EQ(s_code.timings[8], 550);

    /* Noise floor and ceiling */
    ir_learner_init(&s_learner, 2);
    CHECK_EQ(ir_learner_feed(&s_learner, base, 5, 0), IR_FEED_NOISE);
    for (size_t i = 0; i < IR_MAX_TIMINGS + 1; i++) {
        s_t[i] = 500;
    }
    CHECK_EQ(ir_learner_feed(&s_learner, s_t, IR_MAX_TIMINGS + 1, 0), IR_FEED_NOISE);
    CHECK_EQ(ir_learner_presses(&s_learner), 0);
}

static void test_validate(void)
{
    memset(&s_code, 0, sizeof(s_code));
    s_code.proto = IR_PROTO_RAW;
    CHECK_EQ(ir_codec_validate(&s_code, NULL), ESP_ERR_INVALID_ARG); /* no timings */
    s_code.count = 3;
    s_code.timings[0] = 9000;
    s_code.timings[1] = 0;
    s_code.timings[2] = 560;
    CHECK_EQ(ir_codec_validate(&s_code, NULL), ESP_ERR_INVALID_ARG); /* zero timing */
    s_code.timings[1] = 4500;
    CHECK_EQ(ir_codec_validate(&s_code, NULL), ESP_OK);
    s_code.carrier_hz = 5000;
    CHECK_EQ(ir_codec_validate(&s_code, NULL), ESP_ERR_INVALID_ARG);
    s_code.carrier_hz = 0;
    s_code.count = IR_MAX_TIMINGS + 1;
    CHECK_EQ(ir_codec_validate(&s_code, NULL), ESP_ERR_INVALID_SIZE);
    s_code.proto = (ir_proto_t)99; /* not a protocol */
    CHECK_EQ(ir_codec_validate(&s_code, NULL), ESP_ERR_NOT_SUPPORTED);
    CHECK_EQ(ir_codec_validate(NULL, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS), 0);
}

static void text_roundtrip(const ir_code_t *c, const char *expect)
{
    const char *why = NULL;
    CHECK_EQ(ir_codec_format(c, s_text, sizeof(s_text), &why), ESP_OK);
    if (expect) {
        CHECK_STR(s_text, expect);
    }
    size_t where = 0;
    CHECK_EQ(ir_codec_parse(s_text, &s_code2, &why, &where), ESP_OK);
    CHECK_EQ(s_code2.proto, c->proto);
    if (c->proto == IR_PROTO_RAW) {
        CHECK_EQ(s_code2.carrier_hz, c->carrier_hz ? c->carrier_hz : 38000);
        CHECK_EQ(s_code2.count, c->count);
        CHECK(memcmp(s_code2.timings, c->timings, c->count * sizeof(uint16_t)) == 0);
    } else {
        CHECK_EQ(s_code2.value, c->value);
        uint32_t want = c->carrier_hz == 38000 ? 0 : c->carrier_hz;
        CHECK_EQ(s_code2.carrier_hz, want);
    }
}

static void expect_parse_error(const char *text, esp_err_t err)
{
    const char *why = NULL;
    size_t where = 12345;
    memset(&s_code2, 0xAA, sizeof(s_code2));
    esp_err_t got = ir_codec_parse(text, &s_code2, &why, &where);
    s_checks++;
    if (got != err) {
        s_fails++;
        printf("FAIL: parse \"%.60s\" gave 0x%x, want 0x%x\n", text, (unsigned)got,
               (unsigned)err);
    }
    CHECK(why != NULL && why[0] != '\0');
    CHECK(where != 12345);
    CHECK_EQ(s_code2.proto, IR_PROTO_RAW); /* zeroed */
    CHECK_EQ(s_code2.count, 0);
}

static void test_text(void)
{
    set_proto32(&s_code, IR_PROTO_NEC, 0x20DF10EFu);
    text_roundtrip(&s_code, "NEC 20DF10EF");
    set_proto32(&s_code, IR_PROTO_SAMSUNG32, 0xE0E040BFu);
    text_roundtrip(&s_code, "SAMSUNG32 E0E040BF");
    set_proto32(&s_code, IR_PROTO_NEC, 0x0000ABCDu);
    text_roundtrip(&s_code, "NEC 0000ABCD");
    s_code.carrier_hz = 38000; /* the default is not written out */
    text_roundtrip(&s_code, "NEC 0000ABCD");
    s_code.carrier_hz = 36000;
    text_roundtrip(&s_code, "NEC 0000ABCD 36000");

    memset(&s_code, 0, sizeof(s_code));
    s_code.proto = IR_PROTO_RAW;
    s_code.count = 3;
    s_code.timings[0] = 9000;
    s_code.timings[1] = 4500;
    s_code.timings[2] = 560;
    text_roundtrip(&s_code, "RAW 38000 9000 4500 560");
    s_code.carrier_hz = 40000;
    text_roundtrip(&s_code, "RAW 40000 9000 4500 560");

    /* Worst case fits IR_CODE_TEXT_MAX */
    s_code.carrier_hz = 500000;
    s_code.count = IR_MAX_TIMINGS;
    for (size_t i = 0; i < IR_MAX_TIMINGS; i++) {
        s_code.timings[i] = 65535;
    }
    text_roundtrip(&s_code, NULL);
    CHECK(strlen(s_text) < IR_CODE_TEXT_MAX);
    /* Random RAW codes */
    for (int k = 0; k < 200; k++) {
        s_code.carrier_hz = (k & 1) ? 38000 : 0;
        s_code.count = (uint16_t)(1 + (rnd() >> 8) % IR_MAX_TIMINGS);
        for (size_t i = 0; i < s_code.count; i++) {
            s_code.timings[i] = (uint16_t)(1 + (rnd() >> 8) % 65535);
        }
        text_roundtrip(&s_code, NULL);
    }
    /* Learned code through text */
    set_proto32(&s_code, IR_PROTO_SAMSUNG32, rnd32());
    text_roundtrip(&s_code, NULL);

    /* Buffer too small */
    set_proto32(&s_code, IR_PROTO_SAMSUNG32, 0xE0E040BFu);
    const char *why = NULL;
    CHECK_EQ(ir_codec_format(&s_code, s_text, 18, &why), ESP_ERR_INVALID_SIZE);
    CHECK_STR(s_text, "");
    CHECK_EQ(ir_codec_format(&s_code, s_text, 19, &why), ESP_OK);
    CHECK_EQ(ir_codec_format(&s_code, NULL, 19, &why), ESP_ERR_INVALID_ARG);
    s_code.proto = IR_PROTO_RAW; /* count 0 */
    CHECK_EQ(ir_codec_format(&s_code, s_text, sizeof(s_text), &why), ESP_ERR_INVALID_ARG);

    /* Lenient on case and surrounding whitespace */
    size_t where;
    CHECK_EQ(ir_codec_parse("nec 20df10ef", &s_code2, &why, &where), ESP_OK);
    CHECK_EQ(s_code2.proto, IR_PROTO_NEC);
    CHECK_EQ(s_code2.value, 0x20DF10EFu);
    CHECK_EQ(ir_codec_parse("  Samsung32\tE0E040BF\r\n", &s_code2, &why, &where), ESP_OK);
    CHECK_EQ(s_code2.proto, IR_PROTO_SAMSUNG32);
    CHECK_EQ(s_code2.value, 0xE0E040BFu);
    CHECK_EQ(ir_codec_parse("RAW 0 100", &s_code2, &why, &where), ESP_OK);
    CHECK_EQ(s_code2.carrier_hz, 0);

    /* Strict on everything else */
    expect_parse_error("", ESP_ERR_INVALID_ARG);
    expect_parse_error("   ", ESP_ERR_INVALID_ARG);
    expect_parse_error("NEC", ESP_ERR_INVALID_ARG);
    expect_parse_error("NEC 20DF10E", ESP_ERR_INVALID_ARG);
    expect_parse_error("NEC 20DF10EF0", ESP_ERR_INVALID_ARG);
    expect_parse_error("NEC 0x20DF10", ESP_ERR_INVALID_ARG);
    expect_parse_error("NEC 20DF10EG", ESP_ERR_INVALID_ARG);
    expect_parse_error("NEC 20DF10EF extra", ESP_ERR_INVALID_ARG);
    expect_parse_error("NEC 20DF10EF 38000 1", ESP_ERR_INVALID_ARG);
    expect_parse_error("NEC 20DF10EF 9999", ESP_ERR_INVALID_ARG);
    expect_parse_error("SAMSUNG E0E040BF", ESP_ERR_NOT_SUPPORTED);
    expect_parse_error("SONY12 0000000A", ESP_ERR_NOT_SUPPORTED);
    expect_parse_error("NEC,20DF10EF", ESP_ERR_NOT_SUPPORTED);
    expect_parse_error("RAW", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 38000", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 38k 9000", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 9999 9000", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 500001 9000", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 38000 9000 0 560", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 38000 9000 65536", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 38000 9000 -4500", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 38000 9000,4500", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 38000 9000 4500 1e3", ESP_ERR_INVALID_ARG);
    expect_parse_error("RAW 38000 99999999999", ESP_ERR_INVALID_ARG);

    /* 513 timings */
    static char big[IR_CODE_TEXT_MAX * 2];
    size_t pos = (size_t)snprintf(big, sizeof(big), "RAW 38000");
    for (int i = 0; i < IR_MAX_TIMINGS + 1; i++) {
        pos += (size_t)snprintf(big + pos, sizeof(big) - pos, " 560");
    }
    expect_parse_error(big, ESP_ERR_INVALID_SIZE);
    big[strlen(big) - 4] = '\0'; /* exactly 512 */
    CHECK_EQ(ir_codec_parse(big, &s_code2, &why, &where), ESP_OK);
    CHECK_EQ(s_code2.count, IR_MAX_TIMINGS);

    /* Error position points at the bad token */
    CHECK_EQ(ir_codec_parse("RAW 38000 9000 x", &s_code2, &why, &where), ESP_ERR_INVALID_ARG);
    CHECK_EQ(where, 15);
}

/* --- Sony SIRC and Philips RC5 / RC6 (hand-worked frames) --------------------- */

static void check_frame(const ir_code_t *c, bool toggle, const uint16_t *want, size_t n_want)
{
    size_t n = ir_codec_frame_toggle(c, false, toggle, s_t, IR_MAX_TIMINGS);
    CHECK_EQ(n, n_want);
    for (size_t i = 0; i < n && i < n_want; i++) {
        if (s_t[i] != want[i]) {
            CHECK_EQ(s_t[i], want[i]);
            printf("  at timing %u\n", (unsigned)i);
            break;
        }
    }
}

static void test_sirc(void)
{
    /* Sony TV power: command 21, address 1, 12 bits, sent LSB first. */
    memset(&s_code, 0, sizeof(s_code));
    s_code.proto = IR_PROTO_SIRC12;
    s_code.value = 21u | 1u << 7; /* 0x95 */
    static const uint16_t want[] = {2400, 600, 1200, 600, 600, 600, 1200, 600, 600, 600, 1200, 600, 600,
                                    600,  600, 600,  1200, 600, 600, 600, 600, 600, 600, 600, 600};
    check_frame(&s_code, false, want, COUNT_OF(want));
    CHECK_EQ(ir_codec_carrier(&s_code), 40000);
    uint32_t frame_us = ir_codec_sum_us(want, COUNT_OF(want));
    CHECK_EQ(frame_us, 19200);
    CHECK_EQ(ir_codec_gap_us(IR_PROTO_SIRC12, frame_us), 45000 - 19200);
    CHECK_EQ(ir_codec_gap_us(IR_PROTO_SIRC20, 40000), IR_SIRC_MIN_GAP_US);

    s_code.proto = IR_PROTO_SIRC15;
    CHECK_EQ(ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS), 2 + 15 * 2 - 1);
    s_code.proto = IR_PROTO_SIRC20;
    CHECK_EQ(ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS), 2 + 20 * 2 - 1);
    s_code.proto = IR_PROTO_SIRC12;
    s_code.value = 1u << 12; /* 13 bits: too many */
    CHECK(ir_codec_validate(&s_code, NULL) != ESP_OK);
}

static void test_rc5(void)
{
    /* Philips standby: address 0, command 12, toggle 0. Bits S1 S2 T A4..A0
     * C5..C0 = 1 1 0 00000 001100; a one is idle then carrier, 889 us each.
     * S1 shows only its carrier half; S2's carrier and T's merge (1778). */
    memset(&s_code, 0, sizeof(s_code));
    s_code.proto = IR_PROTO_RC5;
    s_code.value = 12;
    static const uint16_t want[] = {889, 889, 1778, 889,                     /* S1 S2 T */
                                    889, 889, 889,  889, 889, 889, 889, 889, /* A4..A1 */
                                    889, 889,                                /* A0 */
                                    889, 889,                                /* C5 */
                                    889, 1778,                               /* C4, C3 idle */
                                    889, 889,  1778,                         /* C3, C2, C1 carrier */
                                    889, 889};                               /* C1 idle, C0 */
    check_frame(&s_code, false, want, COUNT_OF(want));
    CHECK_EQ(ir_codec_carrier(&s_code), 36000);

    /* Toggle 1: T becomes idle then carrier, so S2's carrier stands alone. */
    size_t n = ir_codec_frame_toggle(&s_code, false, true, s_t, IR_MAX_TIMINGS);
    CHECK(n > 0);
    CHECK_EQ(s_t[2], 889);

    /* RC5X: command 64 + 12 clears the field bit, so S2 starts with carrier
     * and merges with S1's. */
    s_code.value = 76;
    n = ir_codec_frame(&s_code, false, s_t, IR_MAX_TIMINGS);
    CHECK(n > 0);
    CHECK_EQ(s_t[0], 1778);
    CHECK_EQ(ir_codec_gap_us(IR_PROTO_RC5, 25000), 113778 - 25000);
    s_code.value = 1u << 12;
    CHECK(ir_codec_validate(&s_code, NULL) != ESP_OK);
}

static void test_rc6(void)
{
    /* RC6 mode 0, address 0, command 12, toggle 0. Leader 6t / 2t, start 1,
     * mode 000, double-width toggle 0, then 16 bits; a one is carrier first. */
    memset(&s_code, 0, sizeof(s_code));
    s_code.proto = IR_PROTO_RC6;
    s_code.value = 12;
    static const uint16_t want[] = {
        2666, 889, 444, 888, 444, 444, 444, 444, 444, 889, 889, /* leader, start, mode, toggle */
        444,  444, 444, 444, 444, 444, 444, 444, 444, 444, 444, /* bits 15..5: zeros */
        444,  444, 444, 444, 444, 444, 444, 444, 444, 444, 444,
        444,  888,                                              /* bit 4 (0), carrier into bit 3 */
        444,  444, 888, 444, 444, 444,                          /* bits 3..0: 1 1 0 0 */
    };
    check_frame(&s_code, false, want, COUNT_OF(want));
    CHECK_EQ(ir_codec_carrier(&s_code), 36000);
    /* Toggle 1: the double-width bit starts with carrier, merging with mode bit 2's. */
    size_t n = ir_codec_frame_toggle(&s_code, false, true, s_t, IR_MAX_TIMINGS);
    CHECK(n > 0);
    CHECK_EQ(s_t[8], 444 + 889);
    s_code.value = 1u << 16;
    CHECK(ir_codec_validate(&s_code, NULL) != ESP_OK);
}

static void test_new_text(void)
{
    char buf[64];
    CHECK(ir_codec_parse("SIRC12 00000095", &s_code, NULL, NULL) == ESP_OK);
    CHECK_EQ(s_code.proto, IR_PROTO_SIRC12);
    CHECK_EQ(s_code.value, 0x95);
    CHECK(ir_codec_format(&s_code, buf, sizeof(buf), NULL) == ESP_OK);
    CHECK_STR(buf, "SIRC12 00000095");
    CHECK(ir_codec_parse("rc5 0000000c", &s_code, NULL, NULL) == ESP_OK);
    CHECK_EQ(s_code.proto, IR_PROTO_RC5);
    CHECK(ir_codec_format(&s_code, buf, sizeof(buf), NULL) == ESP_OK);
    CHECK_STR(buf, "RC5 0000000C");
    CHECK(ir_codec_parse("RC6 0000000C 38000", &s_code, NULL, NULL) == ESP_OK);
    CHECK(ir_codec_format(&s_code, buf, sizeof(buf), NULL) == ESP_OK);
    CHECK_STR(buf, "RC6 0000000C 38000");
    CHECK(ir_codec_parse("SIRC20 000FFFFF", &s_code, NULL, NULL) == ESP_OK);
    CHECK(ir_codec_parse("SIRC12 00001000", &s_code, NULL, NULL) != ESP_OK);
    CHECK(ir_codec_parse("RC6 00010000", &s_code, NULL, NULL) != ESP_OK);
}

int main(void)
{
    test_sirc();
    test_rc5();
    test_rc6();
    test_new_text();
    test_samsung_power_exact();
    test_nec_frame_and_repeat();
    test_roundtrip_jitter();
    test_pack();
    test_unpack();
    test_match();
    test_learner_basic();
    test_learner_samsung_held();
    test_learner_mismatch();
    test_learner_raw_average();
    test_validate();
    test_text();
    printf("%d checks, %d failed\n", s_checks, s_fails);
    return s_fails ? 1 : 0;
}
