#pragma once

/*
 * The screen's size, and how the UI scales to it.
 *
 * Sizes in the UI's code are written for a 1024 x 600 screen (the design
 * size) and pass through UI_S(), which scales them by the screen's height:
 * on 1024 x 600 it changes nothing, on 800 x 480 it makes them 4/5. The
 * fonts are scaled the same way (components/ui/fonts/h<height>/). Widths
 * that must fill the screen come from UI_W instead (UI_WIDE_W, the pad).
 *
 * The screen comes from the board (menuconfig > Hubmote > Board,
 * CONFIG_HM_SCREEN_W/H); the simulator sets UI_W and UI_H itself.
 */

#ifndef UI_W
#include "sdkconfig.h"
#define UI_W CONFIG_HM_SCREEN_W
#define UI_H CONFIG_HM_SCREEN_H
#endif

#define UI_DESIGN_H 600

/* A design size (px at 1024 x 600) on this screen, rounded to the nearest
 * pixel. A constant expression, so it works in static initializers. */
#define UI_S(px) ((px) >= 0 ? ((px) * UI_H + UI_DESIGN_H / 2) / UI_DESIGN_H \
                            : -((-(px) * UI_H + UI_DESIGN_H / 2) / UI_DESIGN_H))
