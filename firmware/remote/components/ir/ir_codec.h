#pragma once

/*
 * Pure IR logic behind ir.c: protocol timings, frame building, RMT symbol
 * packing and unpacking, NEC / SAMSUNG32 decoding, frame matching and
 * averaging, the learning state machine and the text form.
 *
 * Nothing here touches hardware or includes ESP-IDF headers other than
 * esp_err.h (through ir.h), so host_test/ builds it on a PC against a stub
 * esp_err.h. Private to the ir component.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Protocol timings, microseconds ------------------------------------- */

#define IR_NEC_LEAD_MARK      9000
#define IR_NEC_LEAD_SPACE     4500
#define IR_NEC_REPEAT_SPACE   2250
#define IR_SAMSUNG_LEAD_MARK  4500
#define IR_SAMSUNG_LEAD_SPACE 4500
#define IR_BIT_MARK           560
#define IR_ZERO_SPACE         560
#define IR_ONE_SPACE          1690
#define IR_PROTO32_TIMINGS    67 /* leader mark+space, 32 x (mark, space), stop mark */
#define IR_NEC_REPEAT_TIMINGS 3  /* 9000, 2250, 560 */

#define IR_FRAME_PERIOD_US    108000 /* NEC / SAMSUNG32 frame start to frame start */

#define IR_SIRC_LEAD_MARK     2400
#define IR_SIRC_UNIT          600    /* space, zero mark; a one mark is two units */
#define IR_SIRC_PERIOD_US     45000
#define IR_SIRC_MIN_GAP_US    10000
#define IR_SIRC_MIN_FRAMES    3      /* Sony TVs act on the second or third frame */
#define IR_RC5_HALF           889    /* half a bit (bit = 1778 us) */
#define IR_RC5_PERIOD_US      113778
#define IR_RC6_UNIT           444    /* t; the leader is 6t mark, 2t space */
#define IR_RC6_PERIOD_US      106667
#define IR_MIN_GAP_US         40000  /* silence after every frame */
#define IR_DEFAULT_CARRIER_HZ 38000
#define IR_CARRIER_MIN_HZ     10000
#define IR_CARRIER_MAX_HZ     500000

/* --- Decoding tolerances ------------------------------------------------ */

#define IR_DEC_LEAD_PCT  30   /* leader mark and space: +-30% */
#define IR_DEC_MARK_MIN  250  /* bit and stop marks (nominal 560) */
#define IR_DEC_MARK_MAX  1000
#define IR_DEC_SPACE_MIN 250  /* bit spaces (nominal 560 / 1690) */
#define IR_DEC_SPACE_MAX 2600
#define IR_DEC_BIT_SPLIT 1125 /* space >= this is a 1 (midpoint of 560 and 1690) */

/* --- Learning ------------------------------------------------------------ */

#define IR_MIN_FRAME_TIMINGS 6      /* shorter captures are noise */
#define IR_PRESS_GAP_US      120000 /* frames closer than this are one press */
#define IR_MATCH_PCT         25     /* frame matching: +-25% ... */
#define IR_MATCH_MIN_US      150    /* ... but never tighter than +-150 us */
#define IR_MATCH_STOP_MAX_US 1500   /* an extra trailing stop mark is at most this */

/* --- RMT symbol words ---------------------------------------------------- */
/* Same bit layout as rmt_symbol_word_t.val: bits 0-14 duration0, bit 15
 * level0, bits 16-30 duration1, bit 31 level1. Durations are in RMT ticks,
 * which ir.c sets to 1 us. A duration of 0 is the hardware end marker. */

#define IR_SYM_MAX_TICKS 32767
#define IR_SYM_PAD_US    100 /* space that closes a symbol after an odd half */

/* Worst case for one frame: every RAW timing (up to 65535 us) split into 3
 * halves, a pad half, and a gap of up to 4 halves (2 words). */
#define IR_CODEC_MAX_WORDS ((IR_MAX_TIMINGS * 3 + 2) / 2 + 2)

static inline uint32_t ir_codec_word(uint32_t d0, uint32_t l0, uint32_t d1, uint32_t l1)
{
    return (d0 & 0x7FFFu) | ((l0 & 1u) << 15) | ((d1 & 0x7FFFu) << 16) | ((l1 & 1u) << 31);
}

/* --- Codes --------------------------------------------------------------- */

/* ESP_OK if the code can be sent and written as text. *why (optional) gets a
 * short reason otherwise. */
esp_err_t ir_codec_validate(const ir_code_t *code, const char **why);

/* Carrier to use: carrier_hz, or the protocol's default when it is 0. */
uint32_t ir_codec_carrier(const ir_code_t *code);

/* Mark/space timings of one frame. repeat = the NEC repeat code (9000, 2250,
 * 560); for SAMSUNG32 and RAW it is the same as the first frame. Returns the
 * number of timings, 0 if the code is invalid or max is too small. */
size_t ir_codec_frame(const ir_code_t *code, bool repeat, uint16_t *out, size_t max);
/* The same with the RC5 / RC6 toggle bit set or clear (ignored by others). */
size_t ir_codec_frame_toggle(const ir_code_t *code, bool repeat, bool toggle, uint16_t *out, size_t max);

uint32_t ir_codec_sum_us(const uint16_t *t, size_t n);

/* Silence after a frame of frame_us: NEC and SAMSUNG32 keep a 108 ms frame
 * period, never less than 40 ms of silence; RC5 and RC6 keep their ~110 ms
 * period the same way; SIRC keeps 45 ms with at least 10 ms of silence;
 * RAW gets 40 ms. */
uint32_t ir_codec_gap_us(ir_proto_t proto, uint32_t frame_us);

/* --- RMT symbols ---------------------------------------------------------- */

/* TX: packs timings (mark = level 1) into symbol words, splitting durations
 * above IR_SYM_MAX_TICKS, closing an odd final half with a short space, then
 * appending gap_us of silence as whole words. *frame_words (optional) gets
 * the word count without the gap, so words[0 .. *frame_words) is the same
 * frame with no trailing silence. Returns the total word count, 0 if it
 * doesn't fit in max_words. */
size_t ir_codec_pack(const uint16_t *t, size_t n, uint32_t gap_us,
                     uint32_t *words, size_t max_words, size_t *frame_words);

/* RX: converts received symbol words from an active-low receiver (level 0 =
 * carrier seen = mark) into mark/space timings starting with a mark. Stops at
 * the first zero duration (end marker), merges equal-level halves, drops idle
 * before the first mark and the idle after the last one. *overflow
 * (optional) is set when the frame had more than max timings. */
size_t ir_codec_unpack(const uint32_t *words, size_t n_words, uint16_t *out, size_t max,
                       bool *overflow);

/* --- Decoding and matching ----------------------------------------------- */

bool ir_codec_is_nec_repeat(const uint16_t *t, size_t n);

/* NEC (9 ms leader) or SAMSUNG32 (4.5 ms leader) with 32 bits and a stop
 * mark, tolerant timing. *value gets the bits MSB-first in transmit order. */
bool ir_codec_decode(const uint16_t *t, size_t n, ir_proto_t *proto, uint32_t *value);

/* Learning match rule: same number of timings (or the longer one has just an
 * extra trailing space + short stop mark), and every timing within
 * max(25% of the reference, 150 us) of the reference. */
bool ir_codec_frames_match(const uint16_t *ref, size_t n_ref, const uint16_t *t, size_t n);

/* --- Learning state machine ---------------------------------------------- */

typedef enum {
    IR_FEED_NOISE,      /* too short or too long: ignored */
    IR_FEED_REPEAT,     /* NEC repeat code: ignored */
    IR_FEED_SAME_PRESS, /* follows the previous frame too closely: ignored */
    IR_FEED_PRESS,      /* counted */
    IR_FEED_MISMATCH,   /* didn't match the earlier presses */
    IR_FEED_DONE,       /* enough matching presses */
} ir_feed_t;

typedef struct {
    uint32_t sum[IR_MAX_TIMINGS];
    uint8_t cnt[IR_MAX_TIMINGS];  /* frames that had a timing at this index */
    uint16_t avg[IR_MAX_TIMINGS]; /* sum / cnt, rounded */
    uint16_t alt[IR_MAX_TIMINGS]; /* last mismatching frame */
    uint16_t len;                 /* timings in avg (longest matched frame) */
    uint16_t alt_len;             /* 0 = no alternative */
    uint8_t frames;               /* matching presses so far */
    uint8_t needed;
    bool have_last;
    int64_t last_end_us; /* end of the last frame that wasn't noise */
} ir_learner_t;

void ir_learner_init(ir_learner_t *l, int presses_needed);

/* Feeds one captured frame that ended (last mark ended) at end_us. */
ir_feed_t ir_learner_feed(ir_learner_t *l, const uint16_t *t, size_t n, int64_t end_us);

int ir_learner_presses(const ir_learner_t *l);

/* The averaged frame, decoded to NEC / SAMSUNG32 when possible, otherwise
 * RAW. carrier_hz = 38000 either way (the receiver can't measure it). */
void ir_learner_result(const ir_learner_t *l, ir_code_t *out);

/* --- Text form ------------------------------------------------------------ */

/* "NEC 20DF10EF", "SAMSUNG32 E0E040BF", "SIRC12 00000095" (also SIRC15 and
 * SIRC20), "RC5 0000000C", "RC6 0000000C", "RAW 38000 9000 4500 560 ...";
 * ir.h describes each one's value. Codes with a carrier other than the
 * protocol's own get it appended ("NEC 20DF10EF 36000"). */
esp_err_t ir_codec_format(const ir_code_t *code, char *buf, size_t len, const char **why);

/* Strict parser for the form above. Protocol names are case-insensitive,
 * tokens are separated by spaces or tabs, leading and trailing whitespace is
 * allowed. Errors: ESP_ERR_INVALID_ARG (syntax or range), ESP_ERR_NOT_SUPPORTED
 * (unknown protocol), ESP_ERR_INVALID_SIZE (more than IR_MAX_TIMINGS
 * timings). On error *out is zeroed, *why gets the reason and *where the
 * offset of the offending token (both optional). */
esp_err_t ir_codec_parse(const char *text, ir_code_t *out, const char **why, size_t *where);

#ifdef __cplusplus
}
#endif
