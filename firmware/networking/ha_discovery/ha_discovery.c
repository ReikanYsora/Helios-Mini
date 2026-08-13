#include "ha_discovery.h"
#include "mdns.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "ha_discovery";
static bool s_mdns_initialized = false;

int ha_discovery_scan(ha_discovery_result_t *out, int max_results, int timeout_ms)
{
    if (!s_mdns_initialized) {
        if (mdns_init() != ESP_OK) {
            ESP_LOGW(TAG, "mdns_init failed");
            return 0;
        }
        s_mdns_initialized = true;
    }

    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_home-assistant", "_tcp", timeout_ms, max_results, &results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_query_ptr failed: %s", esp_err_to_name(err));
        return 0;
    }

    int count = 0;
    for (mdns_result_t *r = results; r != NULL && count < max_results; r = r->next) {
        const char *base_url = NULL;
        for (size_t i = 0; i < r->txt_count; i++) {
            if (r->txt[i].key != NULL && strcmp(r->txt[i].key, "base_url") == 0 &&
                r->txt[i].value != NULL && r->txt[i].value[0] != '\0') {
                base_url = r->txt[i].value;
                break;
            }
        }

        char ip_str[24] = {0};
        if (r->addr != NULL && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&r->addr->addr.u_addr.ip4));
        }

        if (base_url != NULL) {
            strncpy(out[count].url, base_url, sizeof(out[count].url) - 1);
            out[count].url[sizeof(out[count].url) - 1] = '\0';
        } else if (ip_str[0] != '\0') {
            snprintf(out[count].url, sizeof(out[count].url), "http://%s:%u", ip_str, r->port);
        } else {
            continue; /* nothing usable to build a URL from */
        }

        const char *name = (r->instance_name != NULL) ? r->instance_name
                          : (r->hostname != NULL) ? r->hostname : "Home Assistant";
        strncpy(out[count].name, name, sizeof(out[count].name) - 1);
        out[count].name[sizeof(out[count].name) - 1] = '\0';

        count++;
    }

    mdns_query_results_free(results);
    return count;
}
