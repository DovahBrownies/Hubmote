#pragma once
/* The simulator's stand-in for esp_lvgl_port: one thread, so the lock is
 * always free. */
#include <stdbool.h>
#include <stdint.h>

static inline bool lvgl_port_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true;
}

static inline void lvgl_port_unlock(void)
{
}
