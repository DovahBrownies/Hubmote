#pragma once
/* The simulator's stand-in for ESP-IDF's heap capabilities: plain malloc. */
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DEFAULT  (1 << 12)

#define heap_caps_malloc(size, caps)                     malloc(size)
#define heap_caps_calloc(n, size, caps)                  calloc(n, size)
#define heap_caps_realloc(p, size, caps)                 realloc(p, size)
#define heap_caps_free(p)                                free(p)
#define heap_caps_malloc_prefer(size, n, ...)            malloc(size)
#define heap_caps_realloc_prefer(p, size, n, ...)        realloc(p, size)
#define heap_caps_get_free_size(caps)                    ((size_t)0)
