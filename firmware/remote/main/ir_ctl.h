#pragma once

/* IR side of the controller: the IR TV, learned remotes and the learn
 * flow. All functions run on the controller task except where noted. */

#include <stdbool.h>
#include "ui.h"

void ir_ctl_init(void);

/* A rail or touchpad key meant for the IR TV (UI_TARGET_IR_TV). */
void ir_ctl_key(int key, ui_dir_t dir);
/* Sends the IR TV's key by name (tv_ir.h); false without that key. */
bool ir_ctl_tv_send(const char *key);
/* A button on an IR screen (UI_ACT_IR). */
void ir_ctl_button(ui_target_t target, const char *id, int slot);
void ir_ctl_open_remote(int index);
/* The learned remote on screen: rename or delete it, or one of its buttons
 * (by grid slot). Each saves at once and refreshes the lists. */
void ir_ctl_remote_rename(const char *name);
void ir_ctl_remote_delete(void);
void ir_ctl_button_rename(int slot, const char *label);
void ir_ctl_button_delete(int slot);

void ir_ctl_learn_start(int remote_index, const char *new_name, const char *label);
/* Inputs page: the "HDMI switch" learned remote, slots 0..UI_INPUTS-1. */
void ir_ctl_input(int slot);
void ir_ctl_input_rename(int slot, const char *name);
void ir_ctl_learn_input(int slot);
void ir_ctl_learn_cancel(void);
void ir_ctl_learn_test(void);
void ir_ctl_learn_save(void);

/* The IR TV (tv_ir.h): choosing a code set (brand, try set n, choose set
 * n), a TV with learned keys only, removing it, the volume route, and
 * learning one of its keys. */
void ir_ctl_tv_brand(const char *brand);
void ir_ctl_tv_try(int n);
void ir_ctl_tv_choose(int n);
void ir_ctl_tv_learned(void);
void ir_ctl_tv_remove(void);
void ir_ctl_tv_volume(bool ir);
void ir_ctl_learn_tv_key(const char *key);
