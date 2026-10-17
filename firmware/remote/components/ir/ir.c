/*
 * Infrared send and learn on the ESP32-S3 RMT (ESP-IDF 5.4 driver). The
 * contract is include/ir.h; the pure logic (frames, RMT symbols, decoding,
 * matching, the learning state machine, the text form) is in ir_codec.c.
 *
 * RMT setup, and why:
 *  - Clock: XTAL (40 MHz) for both channels, divided to 1 MHz, so one tick
 *    is 1 us. In DMA mode the driver only takes a no-light-sleep PM lock,
 *    so an APB-clocked channel would change speed if DFS lowered APB. XTAL
 *    doesn't move. It also sets the RX glitch filter's unit: the filter
 *    counts group-clock ticks in an 8-bit field, so at 40 MHz it tops out
 *    at 255 ticks = 6375 ns (3187 ns on APB).
 *  - TX: DMA (only TX channel 3 has it on the S3) with a 512-symbol buffer.
 *    A frame is encoded in one go in task context, so no interrupt has to
 *    refill the 48-symbol RMT block mid-frame. Falls back to RMT memory
 *    ping-pong if no DMA channel is free. Carrier from the code (38 kHz by
 *    default), 33% duty, on marks only. Repeats are queued back to back
 *    with the inter-frame silence encoded as symbols, so the 108 ms NEC /
 *    Samsung frame period doesn't depend on task timing.
 *  - RX: DMA (only RX channel 7 has it) straight into a 512-symbol buffer
 *    (1024 edges; frames are limited to IR_MAX_TIMINGS = 512) that only
 *    exists while learning. Glitch filter 6000 ns (240 of 255 filter
 *    ticks); idle threshold 12 ms ends a frame (longer than the 9 ms NEC
 *    leader, shorter than the ~40 ms gap before the next frame).
 *  - Channels are enabled only while sending or learning: an enabled
 *    channel holds a PM lock that keeps the chip out of light sleep.
 */

#include "ir.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ir_codec.h"

static const char *TAG = "ir";

#define IR_RMT_CLK_SRC        RMT_CLK_SRC_XTAL
#define IR_RMT_GROUP_HZ       40000000u /* XTAL; the RX glitch filter counts these */
#define IR_RMT_RESOLUTION_HZ  1000000u  /* 1 tick = 1 us */
#define IR_RMT_FILTER_MAX     255u      /* 8-bit filter threshold */

#define IR_TX_DMA_SYMBOLS     512 /* DMA buffer: any realistic frame + gap in one go */
#define IR_TX_MEM_SYMBOLS     96  /* fallback: 2 RMT blocks, ping-pong */
#define IR_TX_QUEUE_DEPTH     4
#define IR_TX_DUTY            0.33f
#define IR_TX_MARGIN_MS       1000
#define IR_MAX_REPEATS        100 /* ~11 s of NEC repeat codes */

#define IR_RX_BUF_SYMBOLS     512 /* 1024 edges */
#define IR_RX_MEM_SYMBOLS     96  /* fallback: 2 RMT blocks, ping-pong */
#define IR_RX_GLITCH_NS       6000u
#define IR_RX_IDLE_US         12000u
#define IR_RX_POWER_SETTLE_MS 50
#define IR_RX_QUEUE_LEN       8

#define IR_LEARN_STACK        8192 /* the callback runs on this task */
#define IR_LEARN_PRIO         5
#define IR_LEARN_POLL_MS      500 /* longest single wait; re-checks cancel and deadline */

#define IR_REP_WORDS          8 /* NEC repeat code: 2 words + up to 2 gap words */

_Static_assert(sizeof(rmt_symbol_word_t) == sizeof(uint32_t), "rmt_symbol_word_t is one word");
_Static_assert((uint64_t)IR_RX_GLITCH_NS * (IR_RMT_GROUP_HZ / 1000000u) / 1000u <= IR_RMT_FILTER_MAX,
               "RX glitch filter beyond the 8-bit limit");
_Static_assert(IR_RX_IDLE_US <= IR_SYM_MAX_TICKS, "RX idle threshold is 15 bits of ticks");
_Static_assert(IR_RX_IDLE_US > IR_NEC_LEAD_MARK, "idle must outlast the NEC leader");
_Static_assert(IR_RX_BUF_SYMBOLS * 2 >= IR_MAX_TIMINGS + 2, "RX buffer too small for a frame");

typedef enum {
    IR_MSG_FRAME,
    IR_MSG_CANCEL,
} ir_msg_kind_t;

typedef struct {
    ir_msg_kind_t kind;
    size_t num_symbols;
    const rmt_symbol_word_t *symbols;
    int64_t at_us; /* when the RX channel reported the frame done */
} ir_rx_msg_t;

typedef struct {
    ir_learner_t learner;
    uint16_t frame[IR_MAX_TIMINGS];
    ir_code_t result;
    rmt_symbol_word_t *rx_buf; /* internal, DMA-capable */
    uint32_t timeout_ms;
    int needed;
    ir_learn_cb_t cb;
    void *ctx;
} ir_session_t;

static bool s_inited;

/* TX */
static rmt_channel_handle_t s_tx_chan;
static rmt_encoder_handle_t s_tx_enc;
static SemaphoreHandle_t s_tx_lock;
static uint16_t *s_tx_timings; /* IR_MAX_TIMINGS */
static uint32_t *s_tx_words;   /* IR_CODEC_MAX_WORDS */
static uint32_t s_tx_rep_words[IR_REP_WORDS];
static uint32_t s_tx_carrier_hz;     /* carrier currently applied */
static int64_t s_tx_quiet_until_us; /* silence owed after the last send */

/* RX */
static rmt_channel_handle_t s_rx_chan;
static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_learn_lock;
static volatile bool s_learning;
static volatile bool s_cancel;
static int s_rx_gpio = -1;
static int s_rx_power_gpio = -1;

static const rmt_receive_config_t s_rx_cfg = {
    .signal_range_min_ns = IR_RX_GLITCH_NS,
    .signal_range_max_ns = IR_RX_IDLE_US * 1000u,
};

/* PSRAM first (task-context buffers only), internal RAM if there is none. */
static void *ir_calloc(size_t n, size_t size)
{
    return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_DEFAULT);
}

/* --- Text form (logging wrappers around ir_codec) --------------------------- */

esp_err_t ir_code_to_text(const ir_code_t *code, char *buf, size_t len)
{
    const char *why = NULL;
    esp_err_t err = ir_codec_format(code, buf, len, &why);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ir_code_to_text: %s", why ? why : esp_err_to_name(err));
    }
    return err;
}

esp_err_t ir_code_from_text(const char *text, ir_code_t *out)
{
    const char *why = NULL;
    size_t where = 0;
    esp_err_t err = ir_codec_parse(text, out, &why, &where);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ir_code_from_text: %s (at offset %u)", why ? why : esp_err_to_name(err),
                 (unsigned)where);
    }
    return err;
}

/* --- Init --------------------------------------------------------------------- */

static void ir_teardown(void)
{
    if (s_tx_enc) {
        rmt_del_encoder(s_tx_enc);
        s_tx_enc = NULL;
    }
    if (s_tx_chan) {
        rmt_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }
    if (s_rx_chan) {
        rmt_del_channel(s_rx_chan);
        s_rx_chan = NULL;
    }
    heap_caps_free(s_tx_words);
    s_tx_words = NULL;
    heap_caps_free(s_tx_timings);
    s_tx_timings = NULL;
    if (s_rx_queue) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }
    if (s_tx_lock) {
        vSemaphoreDelete(s_tx_lock);
        s_tx_lock = NULL;
    }
    if (s_learn_lock) {
        vSemaphoreDelete(s_learn_lock);
        s_learn_lock = NULL;
    }
    s_rx_gpio = -1;
    s_rx_power_gpio = -1;
}

static esp_err_t tx_create(int gpio)
{
    rmt_tx_channel_config_t cfg = {
        .gpio_num = (gpio_num_t)gpio,
        .clk_src = IR_RMT_CLK_SRC,
        .resolution_hz = IR_RMT_RESOLUTION_HZ,
        .mem_block_symbols = IR_TX_DMA_SYMBOLS,
        .trans_queue_depth = IR_TX_QUEUE_DEPTH,
        .flags.with_dma = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&cfg, &s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TX DMA unavailable (%s), using RMT memory ping-pong",
                 esp_err_to_name(err));
        cfg.flags.with_dma = 0;
        cfg.mem_block_symbols = IR_TX_MEM_SYMBOLS;
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&cfg, &s_tx_chan), TAG, "TX channel");
    }
    rmt_copy_encoder_config_t enc_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&enc_cfg, &s_tx_enc), TAG, "copy encoder");

    rmt_carrier_config_t carrier = {
        .frequency_hz = IR_DEFAULT_CARRIER_HZ,
        .duty_cycle = IR_TX_DUTY,
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(s_tx_chan, &carrier), TAG, "TX carrier");
    s_tx_carrier_hz = IR_DEFAULT_CARRIER_HZ;

    s_tx_timings = ir_calloc(IR_MAX_TIMINGS, sizeof(uint16_t));
    s_tx_words = ir_calloc(IR_CODEC_MAX_WORDS, sizeof(uint32_t));
    ESP_RETURN_ON_FALSE(s_tx_timings && s_tx_words, ESP_ERR_NO_MEM, TAG, "TX buffers");
    return ESP_OK;
}

static bool IRAM_ATTR rx_done_cb(rmt_channel_handle_t chan, const rmt_rx_done_event_data_t *ed,
                                 void *user)
{
    (void)chan;
    (void)user;
    BaseType_t woken = pdFALSE;
    ir_rx_msg_t m = {
        .kind = IR_MSG_FRAME,
        .num_symbols = ed->num_symbols,
        .symbols = ed->received_symbols,
        .at_us = esp_timer_get_time(),
    };
    xQueueSendFromISR(s_rx_queue, &m, &woken);
    return woken == pdTRUE;
}

static esp_err_t rx_create(int gpio)
{
    rmt_rx_channel_config_t cfg = {
        .gpio_num = (gpio_num_t)gpio,
        .clk_src = IR_RMT_CLK_SRC,
        .resolution_hz = IR_RMT_RESOLUTION_HZ,
        .mem_block_symbols = IR_RX_BUF_SYMBOLS,
        .flags.with_dma = 1,
    };
    esp_err_t err = rmt_new_rx_channel(&cfg, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RX DMA unavailable (%s), using RMT memory ping-pong",
                 esp_err_to_name(err));
        cfg.flags.with_dma = 0;
        cfg.mem_block_symbols = IR_RX_MEM_SYMBOLS;
        ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&cfg, &s_rx_chan), TAG, "RX channel");
    }
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rx_done_cb };
    ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(s_rx_chan, &cbs, NULL), TAG,
                        "RX callbacks");
    return ESP_OK;
}

esp_err_t ir_init(const ir_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "no config");
    ESP_RETURN_ON_FALSE(!s_inited, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    ESP_RETURN_ON_FALSE(cfg->tx_gpio == -1 || GPIO_IS_VALID_OUTPUT_GPIO(cfg->tx_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "bad tx_gpio %d", cfg->tx_gpio);
    ESP_RETURN_ON_FALSE(cfg->rx_gpio == -1 || GPIO_IS_VALID_GPIO(cfg->rx_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "bad rx_gpio %d", cfg->rx_gpio);
    ESP_RETURN_ON_FALSE(cfg->rx_power_gpio == -1 || GPIO_IS_VALID_OUTPUT_GPIO(cfg->rx_power_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "bad rx_power_gpio %d", cfg->rx_power_gpio);
    ESP_RETURN_ON_FALSE(cfg->tx_gpio == -1 || (cfg->tx_gpio != cfg->rx_gpio &&
                                               cfg->tx_gpio != cfg->rx_power_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "tx_gpio shared with another IR pin");
    ESP_RETURN_ON_FALSE(cfg->rx_gpio == -1 || cfg->rx_gpio != cfg->rx_power_gpio,
                        ESP_ERR_INVALID_ARG, TAG, "rx_gpio == rx_power_gpio");

    /* ir_codec packs symbols by hand; make sure it agrees with the driver. */
    rmt_symbol_word_t probe = { .duration0 = 1234, .level0 = 1, .duration1 = 32767, .level1 = 0 };
    ESP_RETURN_ON_FALSE(probe.val == ir_codec_word(1234, 1, 32767, 0), ESP_ERR_INVALID_STATE, TAG,
                        "rmt_symbol_word_t layout differs from ir_codec");

    esp_err_t ret = ESP_OK;
    s_tx_lock = xSemaphoreCreateMutex();
    s_learn_lock = xSemaphoreCreateMutex();
    s_rx_queue = xQueueCreate(IR_RX_QUEUE_LEN, sizeof(ir_rx_msg_t));
    ESP_GOTO_ON_FALSE(s_tx_lock && s_learn_lock && s_rx_queue, ESP_ERR_NO_MEM, fail, TAG,
                      "locks");

    if (cfg->rx_power_gpio >= 0) {
        /* Receiver off until learning starts. */
        gpio_set_level((gpio_num_t)cfg->rx_power_gpio, 0);
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->rx_power_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io), fail, TAG, "rx_power_gpio");
        gpio_set_level((gpio_num_t)cfg->rx_power_gpio, 0);
        s_rx_power_gpio = cfg->rx_power_gpio;
    }
    if (cfg->tx_gpio >= 0) {
        ESP_GOTO_ON_ERROR(tx_create(cfg->tx_gpio), fail, TAG, "TX");
    }
    if (cfg->rx_gpio >= 0) {
        ESP_GOTO_ON_ERROR(rx_create(cfg->rx_gpio), fail, TAG, "RX");
        s_rx_gpio = cfg->rx_gpio;
        if (s_rx_power_gpio >= 0) {
            /* The RMT driver enables the input pull-up. With the receiver
             * unpowered it would feed the receiver through its output's
             * protection diode, so it's only on while learning. */
            gpio_pullup_dis((gpio_num_t)s_rx_gpio);
        }
    }

    s_inited = true;
    ESP_LOGI(TAG, "ready: tx gpio %d, rx gpio %d, rx power gpio %d", cfg->tx_gpio, cfg->rx_gpio,
             cfg->rx_power_gpio);
    return ESP_OK;

fail:
    ir_teardown();
    return ret;
}

/* --- Send ---------------------------------------------------------------------- */

static bool s_tx_toggle; /* RC5 / RC6 toggle bit: flips on every send */

static esp_err_t send_locked(const ir_code_t *code, int repeats)
{
    bool sirc = code->proto == IR_PROTO_SIRC12 || code->proto == IR_PROTO_SIRC15 || code->proto == IR_PROTO_SIRC20;
    if (sirc && repeats < IR_SIRC_MIN_FRAMES - 1) {
        repeats = IR_SIRC_MIN_FRAMES - 1;
    }
    /* First frame, with and without its trailing silence */
    size_t n = ir_codec_frame_toggle(code, false, s_tx_toggle, s_tx_timings, IR_MAX_TIMINGS);
    ESP_RETURN_ON_FALSE(n > 0, ESP_ERR_INVALID_ARG, TAG, "can't build the frame");
    uint32_t frame_us = ir_codec_sum_us(s_tx_timings, n);
    uint32_t gap_us = ir_codec_gap_us(code->proto, frame_us);
    size_t frame_words = 0;
    size_t total_words = ir_codec_pack(s_tx_timings, n, gap_us, s_tx_words, IR_CODEC_MAX_WORDS,
                                       &frame_words);
    ESP_RETURN_ON_FALSE(total_words > 0, ESP_ERR_INVALID_SIZE, TAG, "frame too long");

    /* Repeat frames: the NEC repeat code, otherwise the first frame again */
    const uint32_t *rep_words = s_tx_words;
    size_t rep_total = total_words;
    size_t rep_frame = frame_words;
    uint32_t rep_us = frame_us;
    uint32_t rep_gap_us = gap_us;
    if (code->proto == IR_PROTO_NEC && repeats > 0) {
        uint16_t rt[IR_NEC_REPEAT_TIMINGS];
        size_t rn = ir_codec_frame(code, true, rt, IR_NEC_REPEAT_TIMINGS);
        rep_us = ir_codec_sum_us(rt, rn);
        rep_gap_us = ir_codec_gap_us(IR_PROTO_NEC, rep_us);
        rep_total = ir_codec_pack(rt, rn, rep_gap_us, s_tx_rep_words, IR_REP_WORDS, &rep_frame);
        ESP_RETURN_ON_FALSE(rep_total > 0, ESP_ERR_INVALID_SIZE, TAG, "repeat frame");
        rep_words = s_tx_rep_words;
    }

    uint32_t carrier = ir_codec_carrier(code);
    if (carrier != s_tx_carrier_hz) {
        rmt_carrier_config_t cc = { .frequency_hz = carrier, .duty_cycle = IR_TX_DUTY };
        ESP_RETURN_ON_ERROR(rmt_apply_carrier(s_tx_chan, &cc), TAG, "carrier %" PRIu32, carrier);
        s_tx_carrier_hz = carrier;
    }

    /* The previous send's last frame is still owed its silence. */
    int64_t now = esp_timer_get_time();
    if (now < s_tx_quiet_until_us) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)((s_tx_quiet_until_us - now + 999) / 1000)) + 1);
    }

    ESP_RETURN_ON_ERROR(rmt_enable(s_tx_chan), TAG, "enable TX");
    rmt_transmit_config_t tc = { .loop_count = 0 };
    esp_err_t err = ESP_OK;
    for (int i = 0; i <= repeats && err == ESP_OK; i++) {
        bool last = i == repeats;
        const uint32_t *w = i ? rep_words : s_tx_words;
        size_t nw = i ? (last ? rep_frame : rep_total) : (last ? frame_words : total_words);
        err = rmt_transmit(s_tx_chan, s_tx_enc, w, nw * sizeof(uint32_t), &tc);
    }
    /* Waits per queued transaction, so the longest single one bounds it. */
    uint32_t longest_us = (frame_us + gap_us > rep_us + rep_gap_us) ? frame_us + gap_us
                                                                   : rep_us + rep_gap_us;
    esp_err_t wait = rmt_tx_wait_all_done(s_tx_chan, (int)(longest_us / 1000u) + IR_TX_MARGIN_MS);
    if (err == ESP_OK) {
        err = wait;
    }
    rmt_disable(s_tx_chan);
    s_tx_quiet_until_us = esp_timer_get_time() + (repeats ? rep_gap_us : gap_us);
    if (code->proto == IR_PROTO_RC5 || code->proto == IR_PROTO_RC6) {
        s_tx_toggle = !s_tx_toggle;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t ir_send(const ir_code_t *code, int repeats)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "ir_init not called");
    ESP_RETURN_ON_FALSE(s_tx_chan, ESP_ERR_NOT_SUPPORTED, TAG, "sending disabled (tx_gpio -1)");
    ESP_RETURN_ON_FALSE(repeats >= 0 && repeats <= IR_MAX_REPEATS, ESP_ERR_INVALID_ARG, TAG,
                        "repeats must be 0..%d", IR_MAX_REPEATS);
    const char *why = NULL;
    esp_err_t err = ir_codec_validate(code, &why);
    ESP_RETURN_ON_FALSE(err == ESP_OK, err, TAG, "ir_send: %s", why ? why : "invalid code");

    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    err = send_locked(code, repeats);
    xSemaphoreGive(s_tx_lock);
    return err;
}

/* --- Learn ---------------------------------------------------------------------- */

static void rx_power(bool on)
{
    if (s_rx_power_gpio < 0) {
        return;
    }
    if (on) {
        gpio_set_level((gpio_num_t)s_rx_power_gpio, 1);
        gpio_pullup_en((gpio_num_t)s_rx_gpio);
    } else {
        gpio_pullup_dis((gpio_num_t)s_rx_gpio);
        gpio_set_level((gpio_num_t)s_rx_power_gpio, 0);
    }
}

static esp_err_t rx_arm(ir_session_t *s)
{
    return rmt_receive(s_rx_chan, s->rx_buf, IR_RX_BUF_SYMBOLS * sizeof(rmt_symbol_word_t),
                       &s_rx_cfg);
}

static void report(ir_session_t *s, ir_learn_event_t ev, const ir_code_t *code)
{
    ir_learn_report_t r = {
        .event = ev,
        .presses = ir_learner_presses(&s->learner),
        .presses_needed = s->needed,
        .code = code,
    };
    s->cb(&r, s->ctx);
}

static void log_result(const ir_code_t *c)
{
    if (c->proto == IR_PROTO_RAW) {
        ESP_LOGI(TAG, "learned RAW, %u timings", (unsigned)c->count);
    } else {
        ESP_LOGI(TAG, "learned %s %08" PRIX32, c->proto == IR_PROTO_NEC ? "NEC" : "SAMSUNG32",
                 c->value);
    }
}

static void learn_task(void *arg)
{
    ir_session_t *s = arg;
    ir_learn_event_t final = IR_LEARN_TIMEOUT;
    const int64_t timeout_us = (int64_t)s->timeout_ms * 1000;
    int64_t deadline = 0;
    bool enabled = false;

    if (s_rx_power_gpio >= 0) {
        rx_power(true);
        vTaskDelay(pdMS_TO_TICKS(IR_RX_POWER_SETTLE_MS) + 1);
    }
    esp_err_t err = rmt_enable(s_rx_chan);
    if (err == ESP_OK) {
        enabled = true;
        err = rx_arm(s);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "learn: can't start the receiver: %s", esp_err_to_name(err));
        goto out;
    }
    ESP_LOGI(TAG, "learn: waiting for %d presses", s->needed);

    deadline = esp_timer_get_time() + timeout_us;
    for (;;) {
        if (s_cancel) {
            final = IR_LEARN_CANCELLED;
            break;
        }
        int64_t now = esp_timer_get_time();
        if (now >= deadline) {
            final = IR_LEARN_TIMEOUT;
            break;
        }
        int64_t left_ms = (deadline - now + 999) / 1000;
        if (left_ms > IR_LEARN_POLL_MS) {
            left_ms = IR_LEARN_POLL_MS;
        }
        ir_rx_msg_t m;
        if (xQueueReceive(s_rx_queue, &m, pdMS_TO_TICKS((uint32_t)left_ms) + 1) != pdTRUE) {
            continue;
        }
        if (m.kind == IR_MSG_CANCEL) {
            final = IR_LEARN_CANCELLED;
            break;
        }

        bool over = m.num_symbols >= IR_RX_BUF_SYMBOLS; /* buffer filled up */
        size_t n = 0;
        if (!over) {
            n = ir_codec_unpack((const uint32_t *)m.symbols, m.num_symbols, s->frame,
                                IR_MAX_TIMINGS, &over);
        }
        /* The symbols are copied out; the buffer can take the next frame. */
        err = rx_arm(s);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "learn: receiver stopped: %s", esp_err_to_name(err));
            break;
        }
        if (over) {
            ESP_LOGW(TAG, "learn: frame longer than %d timings, ignored", IR_MAX_TIMINGS);
            continue;
        }

        ir_feed_t fed = ir_learner_feed(&s->learner, s->frame, n,
                                        m.at_us - (int64_t)IR_RX_IDLE_US);
        switch (fed) {
        case IR_FEED_NOISE:
            ESP_LOGD(TAG, "learn: %u timings of noise ignored", (unsigned)n);
            break;
        case IR_FEED_REPEAT:
            ESP_LOGD(TAG, "learn: NEC repeat ignored");
            break;
        case IR_FEED_SAME_PRESS:
            ESP_LOGD(TAG, "learn: resend within the same press ignored");
            break;
        case IR_FEED_PRESS:
            ESP_LOGI(TAG, "learn: press %d/%d (%u timings)", ir_learner_presses(&s->learner),
                     s->needed, (unsigned)n);
            deadline = esp_timer_get_time() + timeout_us;
            report(s, IR_LEARN_PRESS, NULL);
            break;
        case IR_FEED_MISMATCH:
            ESP_LOGI(TAG, "learn: press didn't match (%u timings), %d/%d kept", (unsigned)n,
                     ir_learner_presses(&s->learner), s->needed);
            deadline = esp_timer_get_time() + timeout_us;
            report(s, IR_LEARN_MISMATCH, NULL);
            break;
        case IR_FEED_DONE:
            final = IR_LEARN_DONE;
            goto out;
        }
    }

out:
    /* Release the hardware before the final callback, so the callback may
     * start the next session. */
    if (enabled) {
        rmt_disable(s_rx_chan);
    }
    rx_power(false);
    heap_caps_free(s->rx_buf);
    s->rx_buf = NULL;
    if (final == IR_LEARN_DONE) {
        ir_learner_result(&s->learner, &s->result);
        log_result(&s->result);
    } else {
        ESP_LOGI(TAG, "learn: %s with %d/%d presses",
                 final == IR_LEARN_CANCELLED ? "cancelled" : "timed out",
                 ir_learner_presses(&s->learner), s->needed);
    }

    xSemaphoreTake(s_learn_lock, portMAX_DELAY);
    s_learning = false;
    s_cancel = false;
    xSemaphoreGive(s_learn_lock);

    report(s, final, final == IR_LEARN_DONE ? &s->result : NULL);
    heap_caps_free(s);
    vTaskDelete(NULL);
}

esp_err_t ir_learn_start(int presses, uint32_t timeout_ms, ir_learn_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "ir_init not called");
    ESP_RETURN_ON_FALSE(s_rx_chan, ESP_ERR_NOT_SUPPORTED, TAG, "learning disabled (rx_gpio -1)");
    ESP_RETURN_ON_FALSE(presses >= 2 && presses <= 5, ESP_ERR_INVALID_ARG, TAG,
                        "presses must be 2..5");
    ESP_RETURN_ON_FALSE(timeout_ms > 0, ESP_ERR_INVALID_ARG, TAG, "timeout_ms must be > 0");
    ESP_RETURN_ON_FALSE(cb, ESP_ERR_INVALID_ARG, TAG, "no callback");

    ir_session_t *s = ir_calloc(1, sizeof(*s));
    ESP_RETURN_ON_FALSE(s, ESP_ERR_NO_MEM, TAG, "learn session");
    s->rx_buf = heap_caps_aligned_calloc(64, IR_RX_BUF_SYMBOLS, sizeof(rmt_symbol_word_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s->rx_buf) {
        heap_caps_free(s);
        ESP_LOGE(TAG, "no internal DMA memory for the RX buffer");
        return ESP_ERR_NO_MEM;
    }
    ir_learner_init(&s->learner, presses);
    s->needed = presses;
    s->timeout_ms = timeout_ms;
    s->cb = cb;
    s->ctx = ctx;

    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_learn_lock, portMAX_DELAY);
    if (s_learning) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        xQueueReset(s_rx_queue); /* stale frames or cancel from a past session */
        s_cancel = false;
        s_learning = true;
        if (xTaskCreate(learn_task, "ir_learn", IR_LEARN_STACK, s, IR_LEARN_PRIO, NULL) != pdPASS) {
            s_learning = false;
            err = ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreGive(s_learn_lock);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "learn start: %s", err == ESP_ERR_INVALID_STATE ? "already learning"
                                                                        : "no memory for task");
        heap_caps_free(s->rx_buf);
        heap_caps_free(s);
    }
    return err;
}

esp_err_t ir_learn_cancel(void)
{
    if (!s_inited || !s_learn_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_learn_lock, portMAX_DELAY);
    if (!s_learning) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        /* The flag covers a full queue; the message wakes the task now. */
        s_cancel = true;
        ir_rx_msg_t m = { .kind = IR_MSG_CANCEL };
        xQueueSendToFront(s_rx_queue, &m, 0);
    }
    xSemaphoreGive(s_learn_lock);
    return err;
}

