#pragma once

/*
 * The touchpad's joystick (ui_pad.c), without LVGL, so a host test can
 * drive it with recorded finger movements (host_test/test_pad_joy.c).
 *
 * An analog stick around the point the finger landed. Inside a dead zone
 * nothing happens. Leaving it sends one arrow, its way (the larger axis).
 * Held out there, the arrow repeats, slowly just outside the zone (slow_ms
 * apart) and faster the farther out, up to fast_ms apart at far px and
 * beyond; the first repeat waits first_ms at least, so a swipe that ends
 * with the finger resting a moment is still one move. Pulling back slows
 * it; back in the zone it stops; across to another side, one arrow that
 * way.
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum { PAD_NONE = 0, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT } pad_dir_t;

typedef struct {
    int32_t zone;      /* px around where the finger landed */
    int32_t zone_back; /* px: back inside this, the zone again (so its edge doesn't flicker) */
    int32_t far;       /* px out for the fastest repeat */
    uint32_t slow_ms;  /* the repeat just outside the zone */
    uint32_t fast_ms;  /* the repeat at far and beyond */
    uint32_t first_ms; /* the first repeat comes this long after the arrow at least */
} pad_joy_cfg_t;

typedef struct {
    pad_dir_t dir;    /* the arrow held, PAD_NONE in the zone */
    uint32_t last_ms; /* the last arrow */
    bool repeated;    /* the arrow repeated since it was first sent */
} pad_joy_t;

/* A finger landed. */
void pad_joy_start(pad_joy_t *j);
/* The finger is dx, dy from where it landed, at now (ms): the arrow to send
 * now, or PAD_NONE. Call it on every movement and while it stays still. */
pad_dir_t pad_joy_update(pad_joy_t *j, const pad_joy_cfg_t *cfg, int32_t dx, int32_t dy, uint32_t now);
