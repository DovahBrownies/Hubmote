#pragma once

/*
 * Infrared send and learn on the ESP32-S3 RMT peripheral (ESP-IDF 5.4 RMT
 * driver: driver/rmt_tx.h, driver/rmt_rx.h).
 *
 * Hardware (see firmware/remote/README.md):
 *   - TX: a GPIO driving an IR LED stage (MOSFET + 940 nm LED, or the
 *     Amazon kit's transmitter module for short-range tests). The pin is
 *     active-high: high = LED on.
 *   - RX: a 38 kHz demodulating receiver (1838 / TSOP382xx) whose output
 *     idles high and pulls low while it sees carrier.
 *
 * Threading: ir_send() and the ir_code_* helpers may be called from any
 * task; sends are serialised internally. Learning runs on an internal task
 * and reports through the callback, which runs on that task.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_PROTO_RAW = 0,
    IR_PROTO_NEC,       /* 9 ms / 4.5 ms leader, 562 us bits, 32 bits */
    IR_PROTO_SAMSUNG32, /* 4.5 ms / 4.5 ms leader, 562 us bits, 32 bits */
    IR_PROTO_SIRC12,    /* Sony, 40 kHz: 7-bit command, 5-bit address */
    IR_PROTO_SIRC15,    /* Sony: 7-bit command, 8-bit address */
    IR_PROTO_SIRC20,    /* Sony: 7-bit command, 5-bit address, 8-bit extension */
    IR_PROTO_RC5,       /* Philips, 36 kHz: 5-bit address, 6-bit command (7 with RC5X) */
    IR_PROTO_RC6,       /* Philips RC6 mode 0, 36 kHz: 8-bit address, 8-bit command */
} ir_proto_t;

#define IR_MAX_TIMINGS 512 /* mark/space durations in one learned frame */

typedef struct {
    ir_proto_t proto;
    /* NEC / SAMSUNG32: the 32 data bits in transmit order, first bit sent =
     * most significant bit. This is the notation LIRC, IRDB and most code
     * lists use (Samsung TV power toggle = 0xE0E040BF).
     * SIRC12/15/20: command | address << 7 (| extension << 12 for SIRC20);
     * sent LSB first (Sony TV power: command 21, address 1 = 0x95).
     * RC5: command | address << 7, command 0..127 (64..127 is RC5X).
     * RC6: command | address << 8. */
    uint32_t value;
    uint32_t carrier_hz; /* 0: the protocol's own (36 kHz RC5/RC6, 40 kHz SIRC, else 38 kHz) */
    /* RAW: alternating mark, space, mark ... durations in microseconds,
     * starting with a mark. count is the number of entries. */
    uint16_t count;
    uint16_t timings[IR_MAX_TIMINGS];
} ir_code_t;

typedef struct {
    int tx_gpio;       /* -1 disables sending */
    int rx_gpio;       /* -1 disables learning */
    int rx_power_gpio; /* -1 if the receiver is always powered; otherwise this
                          pin powers it (high = on) only while learning */
} ir_config_t;

esp_err_t ir_init(const ir_config_t *cfg);

/* Blocking until the frame (plus repeats) has been sent. repeats = extra
 * copies after the first: protocol repeat frames for NEC (the 9 ms + 2.25 ms
 * repeat code), a full resend for the others, each after the protocol's
 * frame period (40 ms of silence for RAW). Sony codes are always sent at
 * least three times, as Sony TVs expect. RC5 / RC6 flip their toggle bit
 * on every call, so two presses of the same key count twice. */
esp_err_t ir_send(const ir_code_t *code, int repeats);

/* --- Learning ---------------------------------------------------------- */

typedef enum {
    IR_LEARN_PRESS,     /* one press captured; .presses = how many so far */
    IR_LEARN_MISMATCH,  /* the last press didn't match the earlier ones; the
                           earlier ones are kept, press again */
    IR_LEARN_DONE,      /* .code holds the learned code (decoded if possible) */
    IR_LEARN_TIMEOUT,   /* nothing usable within the timeout */
    IR_LEARN_CANCELLED,
} ir_learn_event_t;

typedef struct {
    ir_learn_event_t event;
    int presses;           /* matching presses captured so far */
    int presses_needed;
    const ir_code_t *code; /* IR_LEARN_DONE only; valid during the callback */
} ir_learn_report_t;

typedef void (*ir_learn_cb_t)(const ir_learn_report_t *report, void *ctx);

/* Captures `presses` matching frames (2..5), each within timeout_ms of the
 * previous one, then reports IR_LEARN_DONE with the averaged code. Frames
 * that decode as NEC or SAMSUNG32 are returned decoded; anything else is
 * returned RAW with carrier_hz = 38000 (the receiver can't measure it). */
esp_err_t ir_learn_start(int presses, uint32_t timeout_ms, ir_learn_cb_t cb, void *ctx);
esp_err_t ir_learn_cancel(void);

/* --- Text form, used for storage and logs -------------------------------- */
/* "SAMSUNG32 E0E040BF", "NEC 20DF10EF", "SIRC12 00000095", "RC5 0000000C",
 * "RC6 0000000C", "RAW 38000 9000 4500 560 560 ..." */
#define IR_CODE_TEXT_MAX 3200
esp_err_t ir_code_to_text(const ir_code_t *code, char *buf, size_t len);
esp_err_t ir_code_from_text(const char *text, ir_code_t *out);

#ifdef __cplusplus
}
#endif
