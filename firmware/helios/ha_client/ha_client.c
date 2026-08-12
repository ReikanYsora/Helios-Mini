#include "ha_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

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

ha_entity_status_t ha_client_get_entity_power(const char *url, const char *token,
                                               const char *entity_id, float *value_out)
{
    if (value_out != NULL) {
        *value_out = 0.0f;
    }
    if (entity_id == NULL || entity_id[0] == '\0') {
        return HA_ENTITY_STATUS_NOT_CONFIGURED;
    }
    if (url == NULL || token == NULL || url[0] == '\0' || token[0] == '\0') {
        return HA_ENTITY_STATUS_UNREACHABLE;
    }

    char full_url[224];
    snprintf(full_url, sizeof(full_url), "%s/api/states/%s", url, entity_id);

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);

    esp_http_client_config_t config = {
        .url = full_url,
        .timeout_ms = HA_CLIENT_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return HA_ENTITY_STATUS_UNREACHABLE;
    }
    esp_http_client_set_header(client, "Authorization", auth_header);

    ha_entity_status_t result = HA_ENTITY_STATUS_UNREACHABLE;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open %s failed: %s", full_url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return HA_ENTITY_STATUS_UNREACHABLE;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status == 401 || status == 403) {
        result = HA_ENTITY_STATUS_UNAUTHORIZED;
    } else if (status == 404) {
        result = HA_ENTITY_STATUS_NOT_FOUND;
    } else if (status != 200) {
        ESP_LOGW(TAG, "unexpected HTTP status %d from %s", status, full_url);
        result = HA_ENTITY_STATUS_UNREACHABLE;
    } else {
        size_t buf_size = (content_length > 0 && content_length < 8192) ? (size_t)content_length + 1 : 4096;
        char *buf = malloc(buf_size);
        if (buf == NULL) {
            result = HA_ENTITY_STATUS_UNREACHABLE;
        } else {
            int total_read = 0;
            int r;
            while (total_read < (int)buf_size - 1 &&
                   (r = esp_http_client_read(client, buf + total_read, buf_size - 1 - total_read)) > 0) {
                total_read += r;
            }
            buf[total_read] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) {
                ESP_LOGW(TAG, "bad JSON from %s", full_url);
                result = HA_ENTITY_STATUS_UNREACHABLE;
            } else {
                const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
                if (!cJSON_IsString(state) || state->valuestring == NULL ||
                    strcmp(state->valuestring, "unavailable") == 0 ||
                    strcmp(state->valuestring, "unknown") == 0) {
                    result = HA_ENTITY_STATUS_UNAVAILABLE;
                } else {
                    char *end = NULL;
                    float parsed = strtof(state->valuestring, &end);
                    if (end == state->valuestring) {
                        result = HA_ENTITY_STATUS_NOT_NUMERIC;
                    } else {
                        float multiplier = 1.0f;
                        const cJSON *attrs = cJSON_GetObjectItemCaseSensitive(root, "attributes");
                        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
                        if (cJSON_IsString(unit) && unit->valuestring != NULL) {
                            if (strcasecmp(unit->valuestring, "kW") == 0) {
                                multiplier = 1000.0f;
                            } else if (strcasecmp(unit->valuestring, "MW") == 0) {
                                multiplier = 1000000.0f;
                            }
                        }
                        if (value_out != NULL) {
                            *value_out = parsed * multiplier;
                        }
                        result = HA_ENTITY_STATUS_OK;
                    }
                }
                cJSON_Delete(root);
            }
            free(buf);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

const char *ha_entity_status_text(ha_entity_status_t status)
{
    switch (status) {
        case HA_ENTITY_STATUS_OK:             return "reporting";
        case HA_ENTITY_STATUS_NOT_CONFIGURED: return "not configured";
        case HA_ENTITY_STATUS_NOT_FOUND:      return "no such entity in Home Assistant";
        case HA_ENTITY_STATUS_UNAVAILABLE:    return "unavailable / unknown in Home Assistant";
        case HA_ENTITY_STATUS_NOT_NUMERIC:    return "state isn't a number \xe2\x80\x94 wrong entity?";
        case HA_ENTITY_STATUS_UNAUTHORIZED:   return "token rejected \xe2\x80\x94 fix it on the Home Assistant page";
        default:                              return "could not reach Home Assistant";
    }
}
