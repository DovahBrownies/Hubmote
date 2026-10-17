#pragma once

/* The "storage" partition (partitions.csv), LittleFS at /storage: learned
 * remotes (STORAGE_MOUNT "/ir", components/ir), modules' files
 * (components/hub, hub_store.c) and the companion's icon cache. */

#include "esp_err.h"

#define STORAGE_MOUNT "/storage"

/* Mounts it, formatting it the first time. Call once, before anything uses it. */
esp_err_t storage_mount(void);
