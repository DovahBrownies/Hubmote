#pragma once

#include <stdbool.h>

/* Every function here must be called with the LVGL port lock held. */
void ui_create(void);
void ui_sync_controls(void);
void ui_set_animate(bool on);

/* Full-screen banner on the top layer. tap_aborts: a tap calls runner_abort(). */
void ui_show_overlay(const char *title, const char *body, bool tap_aborts);
/* Large countdown under the banner text; negative hides it. */
void ui_set_overlay_countdown(int seconds);
void ui_hide_overlay(void);
void ui_set_boot_note(const char *text);
