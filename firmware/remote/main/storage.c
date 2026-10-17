#include "storage.h"

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "storage";

#define PARTITION "storage" /* partitions.csv: never move it, or the learned remotes are lost */

esp_err_t storage_mount(void)
{
    if (esp_littlefs_mounted(PARTITION)) {
        return ESP_OK;
    }
    esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_MOUNT,
        .partition_label = PARTITION,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mounting %s failed: %s", STORAGE_MOUNT, esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(PARTITION, &total, &used);
    ESP_LOGI(TAG, "mounted %s: %u of %u KB used", STORAGE_MOUNT, (unsigned)(used / 1024), (unsigned)(total / 1024));
    return ESP_OK;
}
