#include "ir_ctl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ir.h"
#include "ir_store.h"
#include "storage.h"
#include "tv_ir.h"
#include "ui_icons.h"
#include "sdkconfig.h"

static const char *TAG = "ir_ctl";

#define LEARN_PRESSES   3
#define LEARN_TIMEOUT_MS 15000
#define CHECK_PULSES     8    /* self-check burst: 8 x (600 us on, 600 us off) */
#define CHECK_PULSE_US   600

static bool s_ok;                 /* ir_init succeeded */
static ir_remote_t *s_remote;     /* the learned remote on screen (PSRAM) */
static int s_remote_index = -1;
static ir_remote_info_t s_infos[IR_STORE_MAX_REMOTES];
static size_t s_info_count;
static ir_code_t *s_send;         /* scratch for sends (PSRAM, ~1 KB) */
static ir_code_t *s_learned;      /* written by the IR learn task */
static bool s_have_learned;
static SemaphoreHandle_t s_learned_lock;

static struct {
    int index;     /* existing remote, or -1 for a new one */
    char name[IR_STORE_NAME_MAX];
    char label[IR_STORE_LABEL_MAX];
    int slot;      /* an Inputs page input: this slot; -1: by label */
    char tv_key[12]; /* a key of the IR TV ("hdmi1"), or "" */
} s_pending;

/* The Inputs page is the learned remote with this name. */
#define INPUTS_REMOTE "HDMI switch"
static ir_remote_t *s_inputs; /* loaded copy (PSRAM); name[0] == 0 if none */

static int find_remote(const char *name)
{
    for (size_t i = 0; i < s_info_count; i++) {
        if (strcmp(s_infos[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static ir_button_t *find_slot(ir_remote_t *r, int slot)
{
    for (int i = 0; i < r->button_count; i++) {
        if (r->buttons[i].slot == slot) {
            return &r->buttons[i];
        }
    }
    return NULL;
}

/* Reloads the HDMI switch remote and shows its inputs. */
static void publish_tv(void);

static void publish_inputs(void)
{
    int index = find_remote(INPUTS_REMOTE);
    if (index < 0 || ir_store_load(s_infos[index].id, s_inputs) != ESP_OK) {
        memset(s_inputs, 0, sizeof(*s_inputs));
    }
    const char *names[UI_INPUTS];
    bool learned[UI_INPUTS];
    for (int i = 0; i < UI_INPUTS; i++) {
        const ir_button_t *b = s_inputs->name[0] ? find_slot(s_inputs, i) : NULL;
        names[i] = b ? b->label : "";
        learned[i] = b && b->code[0];
    }
    ui_set_inputs(names, learned);
}

static void publish_remotes(void)
{
    size_t n = 0;
    if (ir_store_list(s_infos, IR_STORE_MAX_REMOTES, &n) != ESP_OK) {
        n = 0;
    }
    /* The IR TV's learned keys are stored like a remote but aren't one. */
    for (size_t i = 0; i < n;) {
        if (strcmp(s_infos[i].id, TV_IR_STORE_ID) == 0) {
            memmove(&s_infos[i], &s_infos[i + 1], (n - 1 - i) * sizeof(s_infos[0]));
            n--;
        } else {
            i++;
        }
    }
    s_info_count = n;
    /* The HDMI switch has its own page (Inputs). Keep it last and out of the
     * remotes list, so the list's indices stay those of s_infos. */
    int inputs = find_remote(INPUTS_REMOTE);
    if (inputs >= 0) {
        ir_remote_info_t keep = s_infos[inputs];
        memmove(&s_infos[inputs], &s_infos[inputs + 1], (n - 1 - (size_t)inputs) * sizeof(keep));
        s_infos[n - 1] = keep;
        n--;
    }
    const char *names[IR_STORE_MAX_REMOTES];
    for (size_t i = 0; i < n; i++) {
        names[i] = s_infos[i].name;
    }
    ui_set_ir_remotes(names, n);
}

#if CONFIG_HM_IR_BOOT_CHECK
/* --- Self-check: does the transmitter reach the receiver? ------------------ */

static SemaphoreHandle_t s_check_done;
static volatile bool s_check_heard;
static volatile int s_check_frames; /* frames seen, matching or not */

static void check_cb(const ir_learn_report_t *r, void *ctx)
{
    if (r->event == IR_LEARN_PRESS || r->event == IR_LEARN_MISMATCH) {
        s_check_frames++;
    }
    if (r->event == IR_LEARN_DONE) {
        s_check_heard = true;
    }
    if (r->event == IR_LEARN_DONE || r->event == IR_LEARN_TIMEOUT || r->event == IR_LEARN_CANCELLED) {
        xSemaphoreGive(s_check_done);
    }
}

static void check_task(void *arg)
{
    ir_code_t *probe = heap_caps_calloc(1, sizeof(ir_code_t), MALLOC_CAP_SPIRAM);
    s_check_done = xSemaphoreCreateBinary();
    if (!probe || !s_check_done) {
        vTaskDelete(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); /* let start-up settle */

    /* Equal pulses: not NEC, Samsung or any common protocol, so nothing in
     * the room reacts. Sent twice, as learning wants two matching frames. */
    probe->proto = IR_PROTO_RAW;
    probe->carrier_hz = 38000;
    probe->count = CHECK_PULSES * 2 - 1;
    for (int i = 0; i < probe->count; i++) {
        probe->timings[i] = CHECK_PULSE_US;
    }
    s_check_frames = 0;
    esp_err_t err = ir_learn_start(2, 1000, check_cb, NULL);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        for (int i = 0; i < 2 && err == ESP_OK; i++) {
            err = ir_send(probe, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (xSemaphoreTake(s_check_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ir_learn_cancel();
            xSemaphoreTake(s_check_done, pdMS_TO_TICKS(500));
        }
    }
    ESP_LOGI(TAG, "IR check (tx GPIO%d -> rx GPIO%d, %d frames): %s", CONFIG_HM_IR_TX_GPIO,
             CONFIG_HM_IR_RX_GPIO, s_check_frames,
             err != ESP_OK      ? esp_err_to_name(err)
             : s_check_heard     ? "the receiver saw the transmitter"
             : s_check_frames > 0 ? "the receiver saw the transmitter, but the frames differ (too close?)"
                                 : "nothing received (transmitter dark, or the two can't see each other)");
    free(probe);
    vSemaphoreDelete(s_check_done);
    vTaskDelete(NULL);
}
#endif

void ir_ctl_init(void)
{
    s_remote = heap_caps_calloc(1, sizeof(ir_remote_t), MALLOC_CAP_SPIRAM);
    s_inputs = heap_caps_calloc(1, sizeof(ir_remote_t), MALLOC_CAP_SPIRAM);
    s_send = heap_caps_calloc(1, sizeof(ir_code_t), MALLOC_CAP_SPIRAM);
    s_learned = heap_caps_calloc(1, sizeof(ir_code_t), MALLOC_CAP_SPIRAM);
    s_learned_lock = xSemaphoreCreateMutex();

    ir_config_t cfg = {
        .tx_gpio = CONFIG_HM_IR_TX_GPIO,
        .rx_gpio = CONFIG_HM_IR_RX_GPIO,
        .rx_power_gpio = CONFIG_HM_IR_RX_POWER_GPIO,
    };
    esp_err_t err = ir_init(&cfg);
    s_ok = err == ESP_OK;
    ESP_LOGI(TAG, "ir: tx GPIO%d, rx GPIO%d: %s", cfg.tx_gpio, cfg.rx_gpio, esp_err_to_name(err));
    /* Boards without free pins for IR (docs/porting.md) are Streamer-only
     * remotes: the UI leaves out what can't work. */
    ui_set_ir_caps(s_ok && cfg.tx_gpio >= 0, s_ok && cfg.rx_gpio >= 0);
    err = ir_store_init(STORAGE_MOUNT "/ir");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage: %s", esp_err_to_name(err));
    }
    tv_ir_init();
    publish_remotes();
    publish_inputs();
    publish_tv();
#if CONFIG_HM_IR_BOOT_CHECK
    if (s_ok && cfg.tx_gpio >= 0 && cfg.rx_gpio >= 0) {
        xTaskCreate(check_task, "ir_check", 3072, NULL, 3, NULL);
    }
#endif
}

/* Sends a code in its text form (ir.h) and logs it under `what`. */
static void send_text(const char *what, const char *code, int repeats)
{
    esp_err_t err = ir_code_from_text(code, s_send);
    if (err == ESP_OK) {
        err = ir_send(s_send, repeats);
    }
    ESP_LOGI(TAG, "%s (%.24s): %s", what, code, esp_err_to_name(err));
}

static void send_tv(const char *key, int repeats)
{
    char code[IR_CODE_TEXT_MAX];
    if (!s_ok || !tv_ir_code(key, code, sizeof(code))) {
        ESP_LOGW(TAG, "tv %s: %s", key, s_ok ? "no code" : "IR not ready");
        return;
    }
    char what[24];
    snprintf(what, sizeof(what), "tv %s", key);
    send_text(what, code, repeats);
}

void ir_ctl_key(int key, ui_dir_t dir)
{
    const char *id = tv_ir_key_for(key);
    if (!id || dir == UI_DIR_END_LONG) {
        return;
    }
    /* A held key sends a few repeats; the TV treats it as held. */
    send_tv(id, dir == UI_DIR_START_LONG ? 3 : 0);
}

void ir_ctl_button(ui_target_t target, const char *id, int slot)
{
    if (target == UI_TARGET_IR_TV) {
        send_tv(id, 0);
        return;
    }
    if (!s_ok || s_remote_index < 0) {
        return;
    }
    for (int i = 0; i < s_remote->button_count; i++) {
        const ir_button_t *b = &s_remote->buttons[i];
        if (b->slot != slot) {
            continue;
        }
        char what[64];
        snprintf(what, sizeof(what), "%s / %s", s_remote->name, b->label);
        send_text(what, b->code, 0);
        return;
    }
}

void ir_ctl_open_remote(int index)
{
    if (index < 0 || (size_t)index >= s_info_count) {
        return;
    }
    if (ir_store_load(s_infos[index].id, s_remote) != ESP_OK) {
        ESP_LOGE(TAG, "can't load remote %s", s_infos[index].id);
        return;
    }
    s_remote_index = index;
    ui_ir_button_t buttons[IR_STORE_MAX_BUTTONS];
    for (int i = 0; i < s_remote->button_count; i++) {
        strlcpy(buttons[i].label, s_remote->buttons[i].label, sizeof(buttons[i].label));
        buttons[i].slot = s_remote->buttons[i].slot;
    }
    ui_show_learned_remote(index, s_remote->name, buttons, s_remote->button_count);
}

/* --- Editing the learned remote on screen ------------------------------- */

static int find_id(const char *id)
{
    for (size_t i = 0; i < s_info_count; i++) {
        if (strcmp(s_infos[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Saves the remote on screen, refreshes the lists and shows it again: a new
 * name can move it in the (sorted) list. */
static void save_remote(const char *what)
{
    char id[sizeof(s_remote->id)];
    strlcpy(id, s_remote->id, sizeof(id));
    esp_err_t err = ir_store_save(s_remote);
    ESP_LOGI(TAG, "%s %s: %s", id, what, esp_err_to_name(err));
    publish_remotes();
    int index = find_id(id);
    if (index >= 0) {
        ir_ctl_open_remote(index);
    }
}

void ir_ctl_remote_rename(const char *name)
{
    if (s_remote_index < 0 || !name || !name[0]) {
        return;
    }
    /* The Inputs page is the remote named INPUTS_REMOTE: no second one. */
    int other = find_remote(name);
    if (strcmp(name, INPUTS_REMOTE) == 0 || (other >= 0 && other != s_remote_index)) {
        ui_notify(ICON_ALERT, "Another remote has that name");
        return;
    }
    strlcpy(s_remote->name, name, sizeof(s_remote->name));
    save_remote("renamed");
}

void ir_ctl_remote_delete(void)
{
    if (s_remote_index < 0) {
        return;
    }
    esp_err_t err = ir_store_delete(s_remote->id);
    ESP_LOGI(TAG, "%s deleted: %s", s_remote->id, esp_err_to_name(err));
    s_remote_index = -1;
    publish_remotes();
    ui_learned_remote_deleted();
}

void ir_ctl_button_rename(int slot, const char *label)
{
    ir_button_t *b = s_remote_index >= 0 && label && label[0] ? find_slot(s_remote, slot) : NULL;
    if (!b) {
        return;
    }
    strlcpy(b->label, label, sizeof(b->label));
    save_remote("button renamed");
}

void ir_ctl_button_delete(int slot)
{
    ir_button_t *b = s_remote_index >= 0 ? find_slot(s_remote, slot) : NULL;
    if (!b) {
        return;
    }
    size_t i = (size_t)(b - s_remote->buttons);
    memmove(b, b + 1, (s_remote->button_count - 1 - i) * sizeof(*b));
    s_remote->button_count--;
    save_remote("button deleted");
}

/* --- Learning ----------------------------------------------------------- */

static void learn_cb(const ir_learn_report_t *r, void *ctx)
{
    /* Runs on the IR learn task; the ui_* calls are thread-safe. */
    switch (r->event) {
    case IR_LEARN_PRESS:
        ui_learn_progress(r->presses, r->presses_needed, false);
        break;
    case IR_LEARN_MISMATCH:
        ui_learn_progress(r->presses, r->presses_needed, true);
        break;
    case IR_LEARN_DONE: {
        xSemaphoreTake(s_learned_lock, portMAX_DELAY);
        *s_learned = *r->code;
        s_have_learned = true;
        xSemaphoreGive(s_learned_lock);
        char summary[96];
        if (r->code->proto == IR_PROTO_RAW) {
            snprintf(summary, sizeof(summary), "Raw signal, %u pulses. Sent at 38 kHz (the receiver can't measure the carrier).",
                     (unsigned)r->code->count);
        } else {
            char text[24];
            ir_code_to_text(r->code, text, sizeof(text));
            snprintf(summary, sizeof(summary), "Decoded as %s", text);
        }
        ESP_LOGI(TAG, "learned: %s", summary);
        ui_learn_result(summary);
        break;
    }
    case IR_LEARN_TIMEOUT:
        ui_learn_failed("Nothing usable came in. Check the receiver on J8 and press the button "
                        "with the remote pointed at it.");
        break;
    case IR_LEARN_CANCELLED:
        break;
    }
}

void ir_ctl_learn_start(int remote_index, const char *new_name, const char *label)
{
    s_pending.slot = -1;
    s_pending.tv_key[0] = '\0';
    s_pending.index = remote_index;
    strlcpy(s_pending.name, new_name ? new_name : "Remote", sizeof(s_pending.name));
    strlcpy(s_pending.label, label ? label : "Button", sizeof(s_pending.label));
    s_have_learned = false;
    if (!s_ok) {
        ui_learn_failed("The IR receiver isn't set up. Check the IR pins in menuconfig (Hubmote).");
        return;
    }
    esp_err_t err = ir_learn_start(LEARN_PRESSES, LEARN_TIMEOUT_MS, learn_cb, NULL);
    if (err != ESP_OK) {
        char reason[64];
        snprintf(reason, sizeof(reason), "Couldn't start learning (%s).", esp_err_to_name(err));
        ui_learn_failed(reason);
    }
}

void ir_ctl_learn_input(int slot)
{
    if (slot < 0 || slot >= UI_INPUTS) {
        return;
    }
    char label[IR_STORE_LABEL_MAX];
    const ir_button_t *b = s_inputs->name[0] ? find_slot(s_inputs, slot) : NULL;
    if (b) {
        strlcpy(label, b->label, sizeof(label));
    } else {
        snprintf(label, sizeof(label), "Input %d", slot + 1);
    }
    ir_ctl_learn_start(find_remote(INPUTS_REMOTE), INPUTS_REMOTE, label);
    s_pending.slot = slot;
}

void ir_ctl_input(int slot)
{
    const ir_button_t *b = s_inputs->name[0] ? find_slot(s_inputs, slot) : NULL;
    if (!s_ok || !b || !b->code[0]) {
        ESP_LOGW(TAG, "input %d: %s", slot + 1, s_ok ? "not learned" : "IR not ready");
        return;
    }
    char what[40];
    snprintf(what, sizeof(what), "input %d (%s)", slot + 1, b->label);
    send_text(what, b->code, 0);
}

void ir_ctl_input_rename(int slot, const char *name)
{
    if (slot < 0 || slot >= UI_INPUTS || !name || !name[0]) {
        return;
    }
    int index = find_remote(INPUTS_REMOTE);
    if (index < 0 || ir_store_load(s_infos[index].id, s_inputs) != ESP_OK) {
        memset(s_inputs, 0, sizeof(*s_inputs));
        strlcpy(s_inputs->name, INPUTS_REMOTE, sizeof(s_inputs->name));
    }
    ir_button_t *b = find_slot(s_inputs, slot);
    if (!b) {
        if (s_inputs->button_count >= IR_STORE_MAX_BUTTONS) {
            return;
        }
        b = &s_inputs->buttons[s_inputs->button_count++];
        memset(b, 0, sizeof(*b));
        b->slot = (uint8_t)slot; /* no code yet: "not learned" */
    }
    strlcpy(b->label, name, sizeof(b->label));
    esp_err_t err = ir_store_save(s_inputs);
    ESP_LOGI(TAG, "input %d named %s: %s", slot + 1, name, esp_err_to_name(err));
    publish_remotes();
    publish_inputs();
}

void ir_ctl_learn_cancel(void)
{
    ir_learn_cancel();
}

void ir_ctl_learn_test(void)
{
    if (!s_have_learned) {
        return;
    }
    xSemaphoreTake(s_learned_lock, portMAX_DELAY);
    *s_send = *s_learned;
    xSemaphoreGive(s_learned_lock);
    esp_err_t err = ir_send(s_send, 0);
    ESP_LOGI(TAG, "test send: %s", esp_err_to_name(err));
}

static int find_free_slot(const ir_remote_t *r)
{
    for (int slot = 0; slot < IR_STORE_MAX_BUTTONS; slot++) {
        bool used = false;
        for (int i = 0; i < r->button_count; i++) {
            used |= r->buttons[i].slot == slot;
        }
        if (!used) {
            return slot;
        }
    }
    return -1;
}

void ir_ctl_learn_save(void)
{
    if (!s_have_learned) {
        return;
    }
    if (s_pending.tv_key[0]) {
        char code[IR_CODE_TEXT_MAX];
        xSemaphoreTake(s_learned_lock, portMAX_DELAY);
        esp_err_t err = ir_code_to_text(s_learned, code, sizeof(code));
        xSemaphoreGive(s_learned_lock);
        if (err == ESP_OK) {
            err = tv_ir_learn(s_pending.tv_key, code);
        }
        if (err != ESP_OK) {
            ui_learn_failed("Couldn't save the key.");
            return;
        }
        publish_tv();
        ui_show_ir_tv();
        return;
    }
    if (s_pending.index >= 0 && (size_t)s_pending.index < s_info_count) {
        if (ir_store_load(s_infos[s_pending.index].id, s_remote) != ESP_OK) {
            ui_learn_failed("Couldn't open that remote to add the button.");
            return;
        }
    } else {
        memset(s_remote, 0, sizeof(*s_remote));
        /* "HDMI switch", then "HDMI switch 2" ... if the name is taken. */
        strlcpy(s_remote->name, s_pending.name, sizeof(s_remote->name));
        for (int n = 2; n < 100; n++) {
            bool taken = false;
            for (size_t i = 0; i < s_info_count; i++) {
                taken |= strcmp(s_infos[i].name, s_remote->name) == 0;
            }
            if (!taken) {
                break;
            }
            snprintf(s_remote->name, sizeof(s_remote->name), "%.24s %d", s_pending.name, n);
        }
    }

    /* An input keeps its slot. Otherwise relearning a label replaces its
     * code, and a new label takes the next free slot. */
    ir_button_t *b = NULL;
    if (s_pending.slot >= 0) {
        b = find_slot(s_remote, s_pending.slot);
    } else {
        for (int i = 0; i < s_remote->button_count; i++) {
            if (strcmp(s_remote->buttons[i].label, s_pending.label) == 0) {
                b = &s_remote->buttons[i];
            }
        }
    }
    if (!b) {
        int slot = s_pending.slot >= 0 ? s_pending.slot : find_free_slot(s_remote);
        if (slot < 0 || s_remote->button_count >= IR_STORE_MAX_BUTTONS) {
            ui_learn_failed("This remote is full (24 buttons).");
            return;
        }
        b = &s_remote->buttons[s_remote->button_count++];
        strlcpy(b->label, s_pending.label, sizeof(b->label));
        b->slot = (uint8_t)slot;
    }
    xSemaphoreTake(s_learned_lock, portMAX_DELAY);
    esp_err_t err = ir_code_to_text(s_learned, b->code, sizeof(b->code));
    xSemaphoreGive(s_learned_lock);
    if (err == ESP_OK) {
        err = ir_store_save(s_remote);
    }
    if (err != ESP_OK) {
        char reason[64];
        snprintf(reason, sizeof(reason), "Couldn't save (%s).", esp_err_to_name(err));
        ui_learn_failed(reason);
        return;
    }
    ESP_LOGI(TAG, "saved %s / %s", s_remote->name, b->label);

    publish_remotes();
    if (s_pending.slot >= 0) {
        publish_inputs();
        ui_show_inputs();
        return;
    }
    for (size_t i = 0; i < s_info_count; i++) {
        if (strcmp(s_infos[i].id, s_remote->id) == 0) {
            ir_ctl_open_remote((int)i);
            return;
        }
    }
}

/* --- The IR TV ------------------------------------------------------------------ */

static size_t s_brand_sets[UI_TV_SETS_MAX]; /* the brand being tried: indices into tv_sets */
static size_t s_brand_set_count;

static void publish_tv(void)
{
    const char *keys[TV_IR_KEYS_MAX];
    size_t n = tv_ir_keys(keys, TV_IR_KEYS_MAX);
    ui_set_ir_tv(tv_ir_name(), tv_ir_detail(), keys, n, tv_ir_volume_ir());
    const char *brands[UI_TV_BRANDS_MAX];
    size_t nb = tv_ir_brands(brands, UI_TV_BRANDS_MAX);
    ui_set_tv_brands(brands, nb);
}

void ir_ctl_tv_brand(const char *brand)
{
    s_brand_set_count = tv_ir_brand_sets(brand, s_brand_sets, sizeof(s_brand_sets) / sizeof(s_brand_sets[0]));
    const char *labels[UI_TV_SETS_MAX];
    for (size_t i = 0; i < s_brand_set_count; i++) {
        labels[i] = tv_ir_key_label(tv_ir_test_key(s_brand_sets[i]));
    }
    ui_tv_setup_sets(brand, labels, s_brand_set_count);
}

void ir_ctl_tv_try(int n)
{
    if (n < 0 || (size_t)n >= s_brand_set_count || !s_ok) {
        return;
    }
    size_t set = s_brand_sets[n];
    const char *key = tv_ir_test_key(set);
    char code[IR_CODE_TEXT_MAX];
    if (tv_ir_set_code(set, key, code, sizeof(code))) {
        char what[32];
        snprintf(what, sizeof(what), "try set %d %s", n + 1, key);
        send_text(what, code, 0);
    }
}

void ir_ctl_tv_choose(int n)
{
    if (n < 0 || (size_t)n >= s_brand_set_count || tv_ir_choose(s_brand_sets[n]) != ESP_OK) {
        return;
    }
    publish_tv();
    ui_show_ir_tv();
}

void ir_ctl_tv_learned(void)
{
    tv_ir_choose_learned();
    publish_tv();
    ui_show_ir_tv();
}

void ir_ctl_tv_remove(void)
{
    tv_ir_remove();
    publish_tv();
}

void ir_ctl_tv_volume(bool ir)
{
    tv_ir_set_volume_ir(ir);
    publish_tv();
}

void ir_ctl_learn_tv_key(const char *key)
{
    ir_ctl_learn_start(-1, "TV", tv_ir_key_label(key));
    strlcpy(s_pending.tv_key, key ? key : "", sizeof(s_pending.tv_key));
}
