#pragma once

/*
 * The TV controlled over IR (docs/ir-tvs.md): a built-in code set
 * (tv_codes.c) plus keys learned from the TV's own remote, which win over
 * the set's. One TV.
 *
 * Kept in NVS (namespace ctrl: irtv_set, irtv_name, irtv_vol) and, for the
 * learned keys, the learned-remote store (id "tv": /storage/ir/tv.json).
 * Controller task only.
 *
 * Keys are named: power, power_on, power_off, vol_up, vol_down, mute,
 * ch_up, ch_down, source, hdmi1..hdmi4, tv, up, down, left, right, enter,
 * return, home, menu, settings, info, exit, play, pause, 0..9.
 */

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define TV_IR_STORE_ID "tv" /* the learned keys' ir_store id; not a learned remote */
#define TV_IR_KEYS_MAX 40

void tv_ir_init(void);
const char *tv_ir_name(void);   /* "Samsung TV", "TV"; "" if none */
const char *tv_ir_detail(void); /* the set's models, or "Learned keys" */

/* Code text for a key: learned first, then the set's. */
bool tv_ir_code(const char *key, char *out, size_t len);
/* Keys this TV has, built-in and learned. Returns how many. */
size_t tv_ir_keys(const char **out, size_t max);
/* The TV key for an Android key code from the rail or touchpad, or NULL. */
const char *tv_ir_key_for(int android_key);
/* "Volume up" for "vol_up". */
const char *tv_ir_key_label(const char *key);

/* --- Choosing a TV ------------------------------------------------------------- */
size_t tv_ir_brands(const char **out, size_t max);
/* Indices into tv_sets of a brand's sets, most complete first. */
size_t tv_ir_brand_sets(const char *brand, size_t *out, size_t max);
/* The key to try a set with: one the TV visibly reacts to (Mute first). */
const char *tv_ir_test_key(size_t set);
bool tv_ir_set_code(size_t set, const char *key, char *out, size_t len);
/* Use built-in set `set` (forgets learned keys). */
esp_err_t tv_ir_choose(size_t set);
/* A TV without a set: every key is learned. */
esp_err_t tv_ir_choose_learned(void);
esp_err_t tv_ir_learn(const char *key, const char *code);
void tv_ir_remove(void);

/* Volume keys: through the Streamer (HDMI-CEC, the default) or the TV's IR. */
bool tv_ir_volume_ir(void);
void tv_ir_set_volume_ir(bool ir);
