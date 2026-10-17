/*
 * LVGL's heap (CONFIG_LV_USE_CUSTOM_MALLOC), in PSRAM. Every screen is
 * built at start-up as many small allocations, and plain malloc() keeps
 * anything under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL in internal RAM, which
 * left Wi-Fi, lwIP and DMA buffers with a few KB.
 */
#include "esp_heap_caps.h"
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, PSRAM_CAPS, MALLOC_CAP_DEFAULT);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc_prefer(p, new_size, 2, PSRAM_CAPS, MALLOC_CAP_DEFAULT);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    LV_UNUSED(mon_p);
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

#endif
