/* The IR TV profile: a built-in code set plus learned keys (tv_ir.h). */
#include "tv_ir.h"

#include <stdio.h>
#include <string.h>
#include "controller.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ir_store.h"
#include "kv.h"
#include "tv_codes.h"
#include "ui.h"

static const char *TAG = "tv_ir";

#define NVS_SET  "irtv_set"
#define NVS_NAME "irtv_name"
#define NVS_VOL  "irtv_vol"

static const struct {
    const char *key;
    const char *label;
} k_keys[] = {
    {"power", "Power"},       {"power_on", "Power on"}, {"power_off", "Power off"}, {"vol_up", "Volume up"},
    {"vol_down", "Volume down"}, {"mute", "Mute"},      {"ch_up", "Channel up"},  {"ch_down", "Channel down"},
    {"source", "Source"},     {"hdmi1", "HDMI 1"},      {"hdmi2", "HDMI 2"},      {"hdmi3", "HDMI 3"},
    {"hdmi4", "HDMI 4"},      {"tv", "TV"},             {"up", "Up"},             {"down", "Down"},
    {"left", "Left"},         {"right", "Right"},       {"enter", "OK"},          {"return", "Back"},
    {"home", "Home"},         {"menu", "Menu"},         {"settings", "Settings"}, {"info", "Info"},
    {"exit", "Exit"},         {"play", "Play"},         {"pause", "Pause"},       {"0", "0"},
    {"1", "1"},               {"2", "2"},               {"3", "3"},               {"4", "4"},
    {"5", "5"},               {"6", "6"},               {"7", "7"},               {"8", "8"},
    {"9", "9"},
};
#define KEY_COUNT (sizeof(k_keys) / sizeof(k_keys[0]))

static const tv_set_t *s_set;
static char s_name[IR_STORE_NAME_MAX];
static char s_detail[64];
static bool s_volume_ir;
static ir_remote_t *s_learned; /* PSRAM; name[0] == 0 when nothing is learned */

static int key_index(const char *key)
{
    for (size_t i = 0; key && i < KEY_COUNT; i++) {
        if (strcmp(k_keys[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const char *tv_ir_key_label(const char *key)
{
    int i = key_index(key);
    return i >= 0 ? k_keys[i].label : key;
}

static const char *set_code(const tv_set_t *set, const char *key)
{
    for (size_t i = 0; set && i < set->key_count; i++) {
        if (strcmp(set->keys[i].key, key) == 0) {
            return set->keys[i].code;
        }
    }
    return NULL;
}

static const char *learned_code(const char *key)
{
    for (int i = 0; s_learned && s_learned->name[0] && i < s_learned->button_count; i++) {
        if (strcmp(s_learned->buttons[i].label, key) == 0 && s_learned->buttons[i].code[0]) {
            return s_learned->buttons[i].code;
        }
    }
    return NULL;
}

static void update_detail(void)
{
    if (s_set) {
        snprintf(s_detail, sizeof(s_detail), "%s", s_set->label[0] ? s_set->label : s_set->brand);
    } else {
        snprintf(s_detail, sizeof(s_detail), "Learned keys");
    }
}

static void nvs_put_str(const char *key, const char *value)
{
    if (value) {
        kv_set_str(CTRL_NVS_NS, key, value);
    } else {
        kv_erase(CTRL_NVS_NS, key);
    }
}

void tv_ir_init(void)
{
    s_learned = heap_caps_calloc(1, sizeof(ir_remote_t), MALLOC_CAP_SPIRAM);
    char id[32] = "";
    kv_get_str(CTRL_NVS_NS, NVS_SET, id, sizeof(id));
    kv_get_str(CTRL_NVS_NS, NVS_NAME, s_name, sizeof(s_name));
    int32_t vol = 0;
    kv_get_i32(CTRL_NVS_NS, NVS_VOL, &vol);
    s_volume_ir = vol != 0;
    for (size_t i = 0; id[0] && i < tv_set_count; i++) {
        if (strcmp(tv_sets[i].id, id) == 0) {
            s_set = &tv_sets[i];
        }
    }
    if (id[0] && !s_set) {
        ESP_LOGW(TAG, "code set %s isn't in this firmware any more", id);
    }
    if (s_learned && ir_store_load(TV_IR_STORE_ID, s_learned) != ESP_OK) {
        memset(s_learned, 0, sizeof(*s_learned));
    }
    update_detail();
    ESP_LOGI(TAG, "%s: set %s, %d learned keys, volume via %s", s_name[0] ? s_name : "no TV",
             s_set ? s_set->id : "none", s_learned ? s_learned->button_count : 0, s_volume_ir ? "IR" : "Streamer");
}

static bool tv_ir_configured(void)
{
    return s_name[0] != '\0';
}

const char *tv_ir_name(void)
{
    return s_name;
}

const char *tv_ir_detail(void)
{
    return s_detail;
}

bool tv_ir_code(const char *key, char *out, size_t len)
{
    if (!tv_ir_configured() || !key) {
        return false;
    }
    const char *code = learned_code(key);
    if (!code) {
        code = set_code(s_set, key);
    }
    if (!code) {
        return false;
    }
    snprintf(out, len, "%s", code);
    return true;
}

size_t tv_ir_keys(const char **out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; tv_ir_configured() && i < KEY_COUNT && n < max; i++) {
        if (learned_code(k_keys[i].key) || set_code(s_set, k_keys[i].key)) {
            out[n++] = k_keys[i].key;
        }
    }
    return n;
}

const char *tv_ir_key_for(int key)
{
    static const char *const digits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    if (key >= UI_KEY_0 && key <= UI_KEY_0 + 9) {
        return digits[key - UI_KEY_0];
    }
    switch (key) {
    case UI_KEY_DPAD_UP: return "up";
    case UI_KEY_DPAD_DOWN: return "down";
    case UI_KEY_DPAD_LEFT: return "left";
    case UI_KEY_DPAD_RIGHT: return "right";
    case UI_KEY_DPAD_CENTER: return "enter";
    case UI_KEY_ENTER: return "enter";
    case UI_KEY_BACK: return "return";
    case UI_KEY_HOME: return "home";
    case UI_KEY_VOLUME_UP: return "vol_up";
    case UI_KEY_VOLUME_DOWN: return "vol_down";
    case UI_KEY_VOLUME_MUTE: return "mute";
    case UI_KEY_POWER: return "power";
    case UI_KEY_MEDIA_PLAY_PAUSE: return "play";
    case UI_KEY_SETTINGS: return "settings";
    case UI_KEY_TV_INPUT: return "source";
    default: return NULL;
    }
}

/* --- Choosing ------------------------------------------------------------------- */

size_t tv_ir_brands(const char **out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < tv_set_count && n < max; i++) {
        if (n == 0 || strcmp(out[n - 1], tv_sets[i].brand) != 0) { /* sets come sorted by brand */
            out[n++] = tv_sets[i].brand;
        }
    }
    return n;
}

size_t tv_ir_brand_sets(const char *brand, size_t *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; brand && i < tv_set_count && n < max; i++) {
        if (strcmp(tv_sets[i].brand, brand) == 0) {
            out[n++] = i;
        }
    }
    return n;
}

const char *tv_ir_test_key(size_t set)
{
    static const char *const order[] = {"mute", "vol_up", "vol_down", "info", "menu", "power", "power_off"};
    for (size_t i = 0; set < tv_set_count && i < sizeof(order) / sizeof(order[0]); i++) {
        if (set_code(&tv_sets[set], order[i])) {
            return order[i];
        }
    }
    return set < tv_set_count && tv_sets[set].key_count ? tv_sets[set].keys[0].key : "power";
}

bool tv_ir_set_code(size_t set, const char *key, char *out, size_t len)
{
    const char *code = set < tv_set_count ? set_code(&tv_sets[set], key) : NULL;
    if (!code) {
        return false;
    }
    snprintf(out, len, "%s", code);
    return true;
}

static void forget_learned(void)
{
    ir_store_delete(TV_IR_STORE_ID);
    if (s_learned) {
        memset(s_learned, 0, sizeof(*s_learned));
    }
}

esp_err_t tv_ir_choose(size_t set)
{
    if (set >= tv_set_count) {
        return ESP_ERR_INVALID_ARG;
    }
    s_set = &tv_sets[set];
    snprintf(s_name, sizeof(s_name), "%s TV", s_set->brand);
    nvs_put_str(NVS_SET, s_set->id);
    nvs_put_str(NVS_NAME, s_name);
    forget_learned();
    update_detail();
    ESP_LOGI(TAG, "TV: %s (set %s, %s)", s_name, s_set->id, s_set->source);
    return ESP_OK;
}

esp_err_t tv_ir_choose_learned(void)
{
    s_set = NULL;
    snprintf(s_name, sizeof(s_name), "TV");
    nvs_put_str(NVS_SET, NULL);
    nvs_put_str(NVS_NAME, s_name);
    forget_learned();
    update_detail();
    ESP_LOGI(TAG, "TV: learned keys only");
    return ESP_OK;
}

esp_err_t tv_ir_learn(const char *key, const char *code)
{
    int index = key_index(key);
    if (index < 0 || !code || !s_learned || !tv_ir_configured()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_learned->name[0]) {
        memset(s_learned, 0, sizeof(*s_learned));
        strlcpy(s_learned->id, TV_IR_STORE_ID, sizeof(s_learned->id));
        strlcpy(s_learned->name, "TV keys", sizeof(s_learned->name));
    }
    ir_button_t *b = NULL;
    uint32_t used = 0; /* the slots taken: ir_store refuses a repeated one */
    for (int i = 0; i < s_learned->button_count; i++) {
        used |= 1u << s_learned->buttons[i].slot;
        if (strcmp(s_learned->buttons[i].label, key) == 0) {
            b = &s_learned->buttons[i];
        }
    }
    bool added = !b;
    if (added) {
        if (s_learned->button_count >= IR_STORE_MAX_BUTTONS) {
            return ESP_ERR_NO_MEM;
        }
        /* The TV's keys are found by name, not by place: any free slot will
         * do (the key's index won't, past the 24 slots). */
        int slot = 0;
        while (slot < IR_STORE_MAX_BUTTONS && (used & (1u << slot))) {
            slot++;
        }
        b = &s_learned->buttons[s_learned->button_count++];
        memset(b, 0, sizeof(*b));
        strlcpy(b->label, key, sizeof(b->label));
        b->slot = (uint8_t)slot;
    }
    strlcpy(b->code, code, sizeof(b->code));
    esp_err_t err = ir_store_save(s_learned);
    if (err != ESP_OK && added) {
        s_learned->button_count--; /* not kept: it would block the next save too */
    }
    ESP_LOGI(TAG, "learned %s (key %d): %s", key, index, esp_err_to_name(err));
    return err;
}

void tv_ir_remove(void)
{
    s_set = NULL;
    s_name[0] = '\0';
    nvs_put_str(NVS_SET, NULL);
    nvs_put_str(NVS_NAME, NULL);
    forget_learned();
    update_detail();
    ESP_LOGI(TAG, "TV removed");
}

bool tv_ir_volume_ir(void)
{
    return s_volume_ir;
}

void tv_ir_set_volume_ir(bool ir)
{
    s_volume_ir = ir;
    kv_set_i32(CTRL_NVS_NS, NVS_VOL, ir ? 1 : 0);
    ESP_LOGI(TAG, "volume via %s", ir ? "the TV's IR" : "the Streamer");
}
