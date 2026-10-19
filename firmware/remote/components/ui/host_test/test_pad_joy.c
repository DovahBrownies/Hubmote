/* The touchpad's joystick (ui_pad_joy.c) driven with finger movements, as
 * the touchpad would feed it every 10 ms. Built and run by CI (Host tests):
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -I.. test_pad_joy.c ../ui_pad_joy.c -o test_pad_joy && ./test_pad_joy
 */
#include <stdio.h>
#include <stdlib.h>

#include "ui_pad_joy.h"

/* The touchpad's values at 1024 x 600 (ui_pad.c). */
static const pad_joy_cfg_t CFG = {.zone = 36, .zone_back = 26, .far = 140, .slow_ms = 1000, .fast_ms = 333, .first_ms = 500};

static int s_failed;

/* A gesture: the finger goes in a straight line from (0, 0) to (x, y) over
 * move_ms, then stays there hold_ms. Counts the arrows each way. */
typedef struct {
    int up, down, left, right;
} counts_t;

static void feed(pad_joy_t *j, int32_t dx, int32_t dy, uint32_t now, counts_t *c)
{
    switch (pad_joy_update(j, &CFG, dx, dy, now)) {
    case PAD_UP: c->up++; break;
    case PAD_DOWN: c->down++; break;
    case PAD_LEFT: c->left++; break;
    case PAD_RIGHT: c->right++; break;
    case PAD_NONE: break;
    }
}

static counts_t gesture(int32_t x, int32_t y, uint32_t move_ms, uint32_t hold_ms)
{
    pad_joy_t j;
    counts_t c = {0};
    pad_joy_start(&j);
    uint32_t t = 1000;
    for (uint32_t e = 0; e <= move_ms; e += 10) {
        int32_t f = move_ms ? (int32_t)(e * 1000 / move_ms) : 1000;
        feed(&j, x * f / 1000, y * f / 1000, t + e, &c);
    }
    for (uint32_t e = 10; e <= hold_ms; e += 10) {
        feed(&j, x, y, t + move_ms + e, &c);
    }
    return c;
}

static void expect(const char *what, int got, int lo, int hi)
{
    if (got < lo || got > hi) {
        printf("FAIL %s: %d, wanted %d..%d\n", what, got, lo, hi);
        s_failed = 1;
    } else {
        printf("ok   %s: %d\n", what, got);
    }
}

int main(void)
{
    counts_t c;

    c = gesture(0, 0, 0, 800);
    expect("a tap: no arrow", c.right + c.left + c.up + c.down, 0, 0);

    c = gesture(25, 10, 150, 300);
    expect("a drift inside the zone: no arrow", c.right + c.left + c.up + c.down, 0, 0);

    c = gesture(120, 15, 120, 0);
    expect("a quick swipe right: one arrow", c.right, 1, 1);
    expect("... and nothing else", c.left + c.up + c.down, 0, 0);

    c = gesture(160, 0, 120, 350);
    expect("a swipe that rests a moment, even far out: still one", c.right, 1, 1);

    c = gesture(45, 0, 150, 2000);
    expect("held 2 s just outside the zone: about 1 a second", c.right, 2, 3);

    c = gesture(90, 0, 150, 2000);
    expect("held 2 s halfway out: in between", c.right, 3, 5);

    c = gesture(160, 0, 150, 3000);
    expect("held 3 s far out: 3 a second at most", c.right, 8, 10);

    c = gesture(0, -70, 150, 0);
    expect("a swipe up: one up", c.up, 1, 1);

    /* Out right, then back to the middle and held there: no more arrows. */
    {
        pad_joy_t j;
        counts_t k = {0};
        pad_joy_start(&j);
        uint32_t t = 0;
        for (int32_t x = 0; x <= 80; x += 8, t += 10) {
            feed(&j, x, 0, t, &k);
        }
        for (int i = 0; i < 150; i++, t += 10) { /* 1.5 s out there */
            feed(&j, 80, 0, t, &k);
        }
        int before = k.right;
        for (int32_t x = 80; x >= 0; x -= 8, t += 10) {
            feed(&j, x, 0, t, &k);
        }
        expect("heading back: at most one more", k.right - before, 0, 1);
        before = k.right;
        for (int i = 0; i < 200; i++, t += 10) { /* 2 s back in the middle */
            feed(&j, 0, 0, t, &k);
        }
        expect("back in the middle: it stops", k.right - before, 0, 0);

        /* Then across to the left: one arrow that way at once. */
        for (int32_t x = 0; x >= -80; x -= 8, t += 10) {
            feed(&j, x, 0, t, &k);
        }
        expect("across to the left: one left", k.left, 1, 1);
    }

    /* Out right, with the finger wandering a little up and down: still right. */
    {
        pad_joy_t j;
        counts_t k = {0};
        pad_joy_start(&j);
        uint32_t t = 0;
        for (int32_t x = 0; x <= 70; x += 7, t += 10) {
            feed(&j, x, 0, t, &k);
        }
        for (int i = 0; i < 100; i++, t += 10) {
            feed(&j, 70, (i % 20) * 4 - 40, t, &k); /* up to 40 px up or down */
        }
        expect("a wandering finger stays right", k.up + k.down, 0, 0);
    }

    printf(s_failed ? "touchpad: FAILED\n" : "touchpad: all passed\n");
    return s_failed;
}
