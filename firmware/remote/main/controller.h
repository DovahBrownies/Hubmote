#pragma once

/* Glue between the UI, Wi-Fi, the Android TV client, the companion and IR.
 * Runs the first-run flow (identity, Wi-Fi, find the TV, pair), forwards UI
 * actions, and owns the screen settings, dimming and the battery readout.
 * The parts: main/ctrl_internal.h. */
/* The NVS namespace of the remote's settings: the controller's, the screen
 * rotation (display.c) and the IR TV (tv_ir.c). */
#define CTRL_NVS_NS "ctrl"

void controller_start(void);
/* The saved screen brightness (1..100), once controller_start() has run. */
int controller_brightness(void);
/* Logs free heap (internal, DMA, PSRAM), labelled with when. */
void controller_log_heap(const char *when);
