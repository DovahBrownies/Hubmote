/* The touchpad's joystick: see ui_pad_joy.h. */
#include "ui_pad_joy.h"

#include <stdbool.h>
#include <stdlib.h>

/* The way the finger points, PAD_NONE in the zone. While an arrow is held
 * its axis wins near-ties, and the zone ends a bit further in. */
static pad_dir_t dir_for(const pad_joy_cfg_t *cfg, int32_t dx, int32_t dy, pad_dir_t held)
{
    int32_t ax = abs(dx), ay = abs(dy);
    int32_t out = ax > ay ? ax : ay;
    if (out < (held ? cfg->zone_back : cfg->zone)) {
        return PAD_NONE;
    }
    bool held_x = held == PAD_LEFT || held == PAD_RIGHT;
    bool horizontal = held ? (held_x ? ax * 3 >= ay * 2 : ax * 2 > ay * 3) : ax >= ay;
    if (horizontal) {
        return dx > 0 ? PAD_RIGHT : PAD_LEFT;
    }
    return dy > 0 ? PAD_DOWN : PAD_UP;
}

/* The repeat for a finger out px along its arrow's axis. */
static uint32_t interval_ms(const pad_joy_cfg_t *cfg, int32_t out)
{
    if (out >= cfg->far) {
        return cfg->fast_ms;
    }
    if (out <= cfg->zone) {
        return cfg->slow_ms;
    }
    return cfg->slow_ms - (uint32_t)((int64_t)(cfg->slow_ms - cfg->fast_ms) * (out - cfg->zone) / (cfg->far - cfg->zone));
}

void pad_joy_start(pad_joy_t *j)
{
    j->dir = PAD_NONE;
    j->last_ms = 0;
    j->repeated = false;
}

pad_dir_t pad_joy_update(pad_joy_t *j, const pad_joy_cfg_t *cfg, int32_t dx, int32_t dy, uint32_t now)
{
    pad_dir_t dir = dir_for(cfg, dx, dy, j->dir);
    if (dir != j->dir) {
        j->dir = dir;
        j->last_ms = now;
        j->repeated = false;
        return dir; /* out of the zone, or across: one arrow (none back in the zone) */
    }
    if (dir == PAD_NONE) {
        return PAD_NONE;
    }
    int32_t out = dir == PAD_LEFT || dir == PAD_RIGHT ? abs(dx) : abs(dy);
    uint32_t wait = interval_ms(cfg, out);
    if (!j->repeated && wait < cfg->first_ms) {
        wait = cfg->first_ms;
    }
    if (now - j->last_ms < wait) {
        return PAD_NONE;
    }
    j->last_ms = now;
    j->repeated = true;
    return dir;
}
