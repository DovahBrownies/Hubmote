#include "storage.h"

#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "storage";

#define PARTITION "storage" /* partitions.csv: never move it, or the learned remotes are lost */

/* True while this firmware is a fresh update, not yet confirmed
 * (ota_mark_good()): a restart would still go back to the one before. */
static bool on_trial(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    return running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
}

esp_err_t storage_mount(void)
{
    if (esp_littlefs_mounted(PARTITION)) {
        return ESP_OK;
    }
    esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_MOUNT,
        .partition_label = PARTITION,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK && on_trial()) {
        /* An update that can't read what the firmware before it saved (a
         * newer LittleFS, say) mustn't format it: back to that firmware,
         * which still can. */
        ESP_LOGE(TAG, "this update can't mount %s: rolling back to the firmware before it", STORAGE_MOUNT);
        esp_ota_mark_app_invalid_rollback_and_reboot(); /* returns only with nothing to go back to */
    }
    if (err != ESP_OK) {
        /* Never formatted (a new or erased board), or another firmware's
         * data, or damaged beyond reading. */
        ESP_LOGW(TAG, "%s can't be mounted: formatting it (learned remotes and cached icons start empty)",
                 STORAGE_MOUNT);
        conf.format_if_mount_failed = true;
        err = esp_vfs_littlefs_register(&conf);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mounting %s failed: %s", STORAGE_MOUNT, esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(PARTITION, &total, &used);
    ESP_LOGI(TAG, "mounted %s: %u of %u KB used", STORAGE_MOUNT, (unsigned)(used / 1024), (unsigned)(total / 1024));
    return ESP_OK;
}
