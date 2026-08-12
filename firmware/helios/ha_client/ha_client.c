#include "ha_client.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "ha_client";

#define HA_CLIENT_TIMEOUT_MS 5000

ha_client_status_t ha_client_test_connection(const char *url, const char *token)
{
    if (url == NULL || token == NULL || url[0] == '\0' || token[0] == '\0') {
        return HA_CLIENT_STATUS_UNKNOWN;
    }

    char full_url[192];
    snprintf(full_url, sizeof(full_url), "%s/api/", url);

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);

    esp_http_client_config_t config = {
        .url = full_url,
        .timeout_ms = HA_CLIENT_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return HA_CLIENT_STATUS_UNREACHABLE;
    }
    esp_http_client_set_header(client, "Authorization", auth_header);

    ha_client_status_t result;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            result = HA_CLIENT_STATUS_OK;
        } else if (status == 401 || status == 403) {
            result = HA_CLIENT_STATUS_UNAUTHORIZED;
        } else {
            ESP_LOGW(TAG, "unexpected HTTP status %d from %s", status, full_url);
            result = HA_CLIENT_STATUS_UNREACHABLE;
        }
    } else {
        ESP_LOGW(TAG, "connection to %s failed: %s", full_url, esp_err_to_name(err));
        result = HA_CLIENT_STATUS_UNREACHABLE;
    }

    esp_http_client_cleanup(client);
    return result;
}

const char *ha_client_status_text(ha_client_status_t status)
{
    switch (status) {
        case HA_CLIENT_STATUS_OK:           return "connected";
        case HA_CLIENT_STATUS_UNAUTHORIZED: return "token rejected \xe2\x80\x94 generate a new one in Home Assistant";
        case HA_CLIENT_STATUS_UNREACHABLE:  return "could not reach this URL";
        default:                            return "not tested yet";
    }
}

const char *ha_client_status_key(ha_client_status_t status)
{
    switch (status) {
        case HA_CLIENT_STATUS_OK:           return "ok";
        case HA_CLIENT_STATUS_UNAUTHORIZED: return "unauthorized";
        case HA_CLIENT_STATUS_UNREACHABLE:  return "unreachable";
        default:                            return "unknown";
    }
}

ha_client_status_t ha_client_status_from_key(const char *key)
{
    if (key == NULL) {
        return HA_CLIENT_STATUS_UNKNOWN;
    }
    if (strcmp(key, "ok") == 0) {
        return HA_CLIENT_STATUS_OK;
    }
    if (strcmp(key, "unauthorized") == 0) {
        return HA_CLIENT_STATUS_UNAUTHORIZED;
    }
    if (strcmp(key, "unreachable") == 0) {
        return HA_CLIENT_STATUS_UNREACHABLE;
    }
    return HA_CLIENT_STATUS_UNKNOWN;
}
