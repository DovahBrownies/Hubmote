#pragma once

/*
 * Learned remotes, kept as JSON files on the LittleFS "storage" partition
 * (see partitions.csv): one file per remote, /ir/<id>.json.
 *
 * Not thread-safe: call from one task (the controller).
 */

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "ir.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IR_STORE_MAX_REMOTES 12
#define IR_STORE_MAX_BUTTONS 24
#define IR_STORE_NAME_MAX    32
#define IR_STORE_LABEL_MAX   20

typedef struct {
    char label[IR_STORE_LABEL_MAX];
    uint8_t slot; /* position in the remote's grid, 0..IR_STORE_MAX_BUTTONS-1 */
    char code[IR_CODE_TEXT_MAX]; /* ir_code_to_text() form */
} ir_button_t;

typedef struct {
    char id[16];                    /* file name stem, [a-z0-9_] */
    char name[IR_STORE_NAME_MAX];   /* shown in the UI */
    uint8_t button_count;
    ir_button_t buttons[IR_STORE_MAX_BUTTONS];
} ir_remote_t; /* ~77 KB: allocate in PSRAM, never on a stack */

typedef struct {
    char id[16];
    char name[IR_STORE_NAME_MAX];
    uint8_t button_count;
} ir_remote_info_t;

/* The remotes live in dir (made if missing), on a filesystem the app has
 * mounted; at most 23 characters ("/storage/ir"). */
esp_err_t ir_store_init(const char *dir);
esp_err_t ir_store_list(ir_remote_info_t *out, size_t max, size_t *count);
esp_err_t ir_store_load(const char *id, ir_remote_t *out);
/* Creates or replaces /ir/<id>.json. An empty id gets one derived from the
 * name (written back into r->id). */
esp_err_t ir_store_save(ir_remote_t *r);
esp_err_t ir_store_delete(const char *id);

#ifdef __cplusplus
}
#endif
