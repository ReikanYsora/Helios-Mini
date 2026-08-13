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
        case HA_CLIENT_STATUS_OK:           return "Connected";
        case HA_CLIENT_STATUS_UNAUTHORIZED: return "Token rejected \xe2\x80\x94 generate a new one in Home Assistant";
        case HA_CLIENT_STATUS_UNREACHABLE:  return "Could not reach this URL";
        default:                            return "Not tested yet";
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

/* Backs ha_client_get_entity_power(): GET `<url>/api/states/<entity_id>`,
 * parse `state` as a float, and report the unit_of_measurement string too
 * (truncated into unit_out) so the caller can convert kW/MW to W. */
static ha_entity_status_t fetch_entity_state(const char *url, const char *token, const char *entity_id,
                                              float *value_out, char *unit_out, size_t unit_out_len)
{
    if (value_out != NULL) {
        *value_out = 0.0f;
    }
    if (unit_out != NULL && unit_out_len > 0) {
        unit_out[0] = '\0';
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
                        if (value_out != NULL) {
                            *value_out = parsed;
                        }
                        if (unit_out != NULL && unit_out_len > 0) {
                            const cJSON *attrs = cJSON_GetObjectItemCaseSensitive(root, "attributes");
                            const cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
                            if (cJSON_IsString(unit) && unit->valuestring != NULL) {
                                strncpy(unit_out, unit->valuestring, unit_out_len - 1);
                                unit_out[unit_out_len - 1] = '\0';
                            }
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

ha_entity_status_t ha_client_get_entity_power(const char *url, const char *token,
                                               const char *entity_id, float *value_out)
{
    float raw = 0.0f;
    char unit[16] = {0};
    ha_entity_status_t status = fetch_entity_state(url, token, entity_id, &raw, unit, sizeof(unit));
    if (status == HA_ENTITY_STATUS_OK && value_out != NULL) {
        float multiplier = 1.0f;
        if (strcasecmp(unit, "kW") == 0) {
            multiplier = 1000.0f;
        } else if (strcasecmp(unit, "MW") == 0) {
            multiplier = 1000000.0f;
        }
        *value_out = raw * multiplier;
    }
    return status;
}

bool ha_client_get_instance_info(const char *url, const char *token, ha_instance_info_t *out)
{
    if (url == NULL || token == NULL || url[0] == '\0' || token[0] == '\0') {
        return false;
    }

    char full_url[192];
    snprintf(full_url, sizeof(full_url), "%s/api/config", url);

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);

    esp_http_client_config_t config = {
        .url = full_url,
        .timeout_ms = HA_CLIENT_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return false;
    }
    esp_http_client_set_header(client, "Authorization", auth_header);

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open %s failed: %s", full_url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status == 200) {
        size_t buf_size = (content_length > 0 && content_length < 8192) ? (size_t)content_length + 1 : 4096;
        char *buf = malloc(buf_size);
        if (buf != NULL) {
            int total_read = 0;
            int r;
            while (total_read < (int)buf_size - 1 &&
                   (r = esp_http_client_read(client, buf + total_read, buf_size - 1 - total_read)) > 0) {
                total_read += r;
            }
            buf[total_read] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root != NULL) {
                const cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, "latitude");
                const cJSON *lon = cJSON_GetObjectItemCaseSensitive(root, "longitude");
                if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon) && out != NULL) {
                    ha_instance_info_t info = {0};
                    info.latitude = lat->valuedouble;
                    info.longitude = lon->valuedouble;

                    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "location_name");
                    if (cJSON_IsString(name) && name->valuestring != NULL) {
                        strncpy(info.location_name, name->valuestring, sizeof(info.location_name) - 1);
                    }
                    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
                    if (cJSON_IsString(state) && state->valuestring != NULL) {
                        strncpy(info.state, state->valuestring, sizeof(info.state) - 1);
                    }

                    *out = info;
                    ok = true;
                } else {
                    ESP_LOGW(TAG, "no latitude/longitude in %s response", full_url);
                }
                cJSON_Delete(root);
            } else {
                ESP_LOGW(TAG, "bad JSON from %s", full_url);
            }
            free(buf);
        }
    } else {
        ESP_LOGW(TAG, "unexpected HTTP status %d from %s", status, full_url);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

const char *ha_entity_status_text(ha_entity_status_t status)
{
    switch (status) {
        case HA_ENTITY_STATUS_OK:             return "Reporting";
        case HA_ENTITY_STATUS_NOT_CONFIGURED: return "Not configured";
        case HA_ENTITY_STATUS_NOT_FOUND:      return "No such entity in Home Assistant";
        case HA_ENTITY_STATUS_UNAVAILABLE:    return "Unavailable / unknown in Home Assistant";
        case HA_ENTITY_STATUS_NOT_NUMERIC:    return "State isn't a number \xe2\x80\x94 wrong entity?";
        case HA_ENTITY_STATUS_UNAUTHORIZED:   return "Token rejected \xe2\x80\x94 fix it on the Home Assistant page";
        default:                              return "Could not reach Home Assistant";
    }
}
