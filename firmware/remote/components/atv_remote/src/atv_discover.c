#include <stdio.h>
#include <string.h>

#include "atv_remote.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "mdns.h"

static const char *TAG = "atv_mdns";

#define ATV_MDNS_SERVICE     "_androidtvremote2"
#define ATV_MDNS_PROTO       "_tcp"
#define ATV_MDNS_MAX_RESULTS 16
#define ATV_MDNS_A_TIMEOUT   1000
#define ATV_MDNS_A_QUERIES   2 /* at most this many 1 s lookups per discovery */

/* Blocking PTR query for _androidtvremote2._tcp. Initializes the mDNS
 * component if nobody has yet and leaves it running (it may be shared with
 * the rest of the firmware, so it is never torn down here). */
esp_err_t atv_discover(atv_device_t *out, size_t max, size_t *found, uint32_t timeout_ms)
{
    if (!found || (!out && max)) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = 0;
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return err;
    }

    mdns_result_t *results = NULL;
    err = mdns_query_ptr(ATV_MDNS_SERVICE, ATV_MDNS_PROTO, timeout_ms, ATV_MDNS_MAX_RESULTS, &results);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t n = 0;
    int a_queries = 0;
    for (mdns_result_t *r = results; r && n < max; r = r->next) {
        if (!r->instance_name) {
            continue;
        }
        char ip[16] = "";
        for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
            if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                snprintf(ip, sizeof(ip), IPSTR, IP2STR(&a->addr.u_addr.ip4));
                break;
            }
        }
        if (!ip[0] && r->hostname && a_queries++ < ATV_MDNS_A_QUERIES) {
            /* PTR answer without an A record in the additional section. */
            esp_ip4_addr_t a4;
            if (mdns_query_a(r->hostname, ATV_MDNS_A_TIMEOUT, &a4) == ESP_OK) {
                snprintf(ip, sizeof(ip), IPSTR, IP2STR(&a4));
            }
        }
        if (!ip[0]) {
            ESP_LOGD(TAG, "\"%s\": no IPv4 address", r->instance_name);
            continue;
        }
        bool dup = false;
        for (size_t i = 0; i < n && !dup; i++) {
            dup = strcmp(out[i].host, ip) == 0 && strncmp(out[i].name, r->instance_name, sizeof(out[i].name) - 1) == 0;
        }
        if (dup) {
            continue; /* same TV seen on another interface / IP protocol */
        }
        strlcpy(out[n].name, r->instance_name, sizeof(out[n].name));
        strlcpy(out[n].host, ip, sizeof(out[n].host));
        out[n].port = r->port ? r->port : 6466;
        ESP_LOGI(TAG, "found \"%s\" at %s:%u", out[n].name, out[n].host, out[n].port);
        n++;
    }
    mdns_query_results_free(results);
    *found = n;
    return ESP_OK;
}
