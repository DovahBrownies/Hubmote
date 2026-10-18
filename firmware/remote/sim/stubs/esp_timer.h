#pragma once
/* esp_timer for the simulator: timers never fire, and time stands still. */
#include <stdint.h>
#include "esp_err.h"

typedef struct esp_timer *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *arg);
    void *arg;
    const char *name;
} esp_timer_create_args_t;

static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    (void)args;
    *out = 0;
    return ESP_OK;
}

static inline esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t us)
{
    (void)timer;
    (void)us;
    return ESP_OK;
}

static inline esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    (void)timer;
    return ESP_OK;
}

static inline int64_t esp_timer_get_time(void)
{
    return 0;
}
