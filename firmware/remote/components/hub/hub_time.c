/* Clock sync (SNTP) and local time from a reported UTC offset. */
#include "hub_time.h"

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "hub_time";

#define VALID_AFTER 1700000000 /* 2023-11: anything earlier is the unset clock */

static bool s_started;
static int32_t s_offset;

void hub_time_start(void)
{
    if (s_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    s_started = err == ESP_OK;
    ESP_LOGI(TAG, "clock sync: %s", esp_err_to_name(err));
}

bool hub_time_valid(void)
{
    return time(NULL) > VALID_AFTER;
}

void hub_time_set_utc_offset(int32_t seconds)
{
    s_offset = seconds;
}

int32_t hub_time_utc_offset(void)
{
    return s_offset;
}

void hub_time_to_local(time_t t, struct tm *out)
{
    time_t local = t + s_offset;
    gmtime_r(&local, out);
}

bool hub_time_local(struct tm *out)
{
    if (!hub_time_valid()) {
        return false;
    }
    hub_time_to_local(time(NULL), out);
    return true;
}
