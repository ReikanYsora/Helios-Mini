#include "irradiance_model.h"
#include "sun_math.h"
#include "ha_client.h"
#include "storage.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "irradiance_model";
static const char *NVS_NAMESPACE = "helios_mini"; /* same namespace/keys networking/settings_server writes ha_url/ha_token to */

#define IRRADIANCE_HA_URL_LEN   192
#define IRRADIANCE_HA_TOKEN_LEN 256

#define IRRADIANCE_TASK_STACK    6144
#define IRRADIANCE_TASK_PRIORITY 3
/* The background task wakes on this cadence. Location (once resolved,
 * never re-fetched) is attempted every tick for free; cloud cover is
 * throttled separately - see CLOUD_REFRESH_TICKS below. */
#define IRRADIANCE_POLL_INTERVAL_MS (60 * 1000)
#define CLOUD_REFRESH_TICKS  20  /* Open-Meteo's hourly data barely changes faster than 20 min */

/* A device clock that hasn't SNTP-synced yet reads as some time near the
 * epoch - computing sun position from that would silently show wrong
 * data as if it were live. Treat "before this" as "not synced yet" (any
 * date before 2024, comfortably behind any real deployment). */
#define TIME_SANE_THRESHOLD ((time_t)1704067200)

static SemaphoreHandle_t s_mutex; /* guards every s_* field below */
static bool s_have_ha_credentials = false; /* true once /ha has a URL+token stored, whether or not location has resolved yet */
static bool s_have_location = false;
static double s_lat = 0.0;
static double s_lon = 0.0;
static bool s_have_cloud = false;
static float s_cloud_cover_pct = 0.0f;

static bool load_ha_credentials(char *url_out, size_t url_len, char *token_out, size_t token_len)
{
    url_out[0] = '\0';
    token_out[0] = '\0';
    bool have_url = storage_get_string(NVS_NAMESPACE, "ha_url", url_out, url_len) == ESP_OK && url_out[0] != '\0';
    bool have_token = storage_get_string(NVS_NAMESPACE, "ha_token", token_out, token_len) == ESP_OK && token_out[0] != '\0';
    return have_url && have_token;
}

/* Fetches Open-Meteo's hourly cloud_cover_low/mid/high for the current UTC
 * hour and blends them the same way Helios's own fetchHomePointData()
 * does: effective = low + 0.6*mid + 0.2*high, capped at 100 - this beats
 * the API's raw total cloud_cover (a satellite view that over-counts high
 * cirrus on an otherwise clear day) for both ground perception and
 * shortwave attenuation. */
static bool fetch_cloud_cover(double lat, double lon, float *out_pct)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&hourly=cloud_cover_low,cloud_cover_mid,cloud_cover_high&forecast_days=2&timezone=UTC",
             lat, lon);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach, /* HTTPS, public internet - not Home Assistant's own (often plain-HTTP LAN) client */
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return false;
    }

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open-meteo request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "open-meteo returned HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    size_t buf_size = (content_length > 0 && content_length < 65536) ? (size_t)content_length + 1 : 32768;
    char *buf = malloc(buf_size);
    if (buf != NULL) {
        int total = 0, r;
        while (total < (int)buf_size - 1 &&
               (r = esp_http_client_read(client, buf + total, buf_size - 1 - total)) > 0) {
            total += r;
        }
        buf[total] = '\0';

        cJSON *root = cJSON_Parse(buf);
        if (root != NULL) {
            cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
            cJSON *times = cJSON_GetObjectItemCaseSensitive(hourly, "time");
            cJSON *low = cJSON_GetObjectItemCaseSensitive(hourly, "cloud_cover_low");
            cJSON *mid = cJSON_GetObjectItemCaseSensitive(hourly, "cloud_cover_mid");
            cJSON *high = cJSON_GetObjectItemCaseSensitive(hourly, "cloud_cover_high");
            if (cJSON_IsArray(times) && cJSON_IsArray(low) && cJSON_IsArray(mid) && cJSON_IsArray(high)) {
                /* Open-Meteo (timezone=UTC) returns "YYYY-MM-DDTHH:00" slots with
                 * no offset - match the current UTC hour exactly; fall back to
                 * the first slot rather than indexing nothing if it's ever
                 * missing (shouldn't happen with forecast_days=2). */
                time_t now = time(NULL);
                struct tm tm_now;
                gmtime_r(&now, &tm_now);
                /* "YYYY-MM-DDTHH:00" is 16 chars + NUL - sized with real headroom
                 * because GCC's format-truncation checker can't bound %04d/%02d
                 * against the runtime range of tm_year/tm_mon/etc and flags a
                 * narrower buffer as -Werror=format-truncation (same recurring
                 * class as provisioning.c's link[] buffer). */
                char want[64];
                snprintf(want, sizeof(want), "%04d-%02d-%02dT%02d:00",
                         tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour);

                int n = cJSON_GetArraySize(times);
                int idx = -1;
                for (int i = 0; i < n; i++) {
                    cJSON *t = cJSON_GetArrayItem(times, i);
                    if (cJSON_IsString(t) && t->valuestring != NULL && strcmp(t->valuestring, want) == 0) {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0 && n > 0) {
                    idx = 0;
                }
                if (idx >= 0) {
                    cJSON *lo = cJSON_GetArrayItem(low, idx);
                    cJSON *mi = cJSON_GetArrayItem(mid, idx);
                    cJSON *hi = cJSON_GetArrayItem(high, idx);
                    double lc = cJSON_IsNumber(lo) ? lo->valuedouble : 0.0;
                    double mc = cJSON_IsNumber(mi) ? mi->valuedouble : 0.0;
                    double hc = cJSON_IsNumber(hi) ? hi->valuedouble : 0.0;
                    if (lc < 0.0) lc = 0.0; else if (lc > 100.0) lc = 100.0;
                    if (mc < 0.0) mc = 0.0; else if (mc > 100.0) mc = 100.0;
                    if (hc < 0.0) hc = 0.0; else if (hc > 100.0) hc = 100.0;
                    double eff = lc + 0.6 * mc + 0.2 * hc;
                    if (eff > 100.0) {
                        eff = 100.0;
                    }
                    *out_pct = (float)eff;
                    ok = true;
                }
            } else {
                ESP_LOGW(TAG, "unexpected open-meteo response shape");
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG, "bad JSON from open-meteo");
        }
        free(buf);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

static void irradiance_task(void *arg)
{
    (void)arg;
    int cloud_refresh_countdown = 0;

    for (;;) {
        bool have_location;
        double lat = 0.0, lon = 0.0;
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            have_location = s_have_location;
            lat = s_lat;
            lon = s_lon;
            xSemaphoreGive(s_mutex);
        } else {
            have_location = false;
        }

        if (!have_location) {
            /* No countdown here (unlike cloud cover below) - a REST GET to
             * Home Assistant's own LAN is cheap, and this only actually
             * fires on the poll tick(s) right after boot before Wi-Fi/HA
             * are up; once it succeeds once it never runs again. */
            char url[IRRADIANCE_HA_URL_LEN] = {0};
            char token[IRRADIANCE_HA_TOKEN_LEN] = {0};
            bool have_credentials = load_ha_credentials(url, sizeof(url), token, sizeof(token));
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_have_ha_credentials = have_credentials;
                xSemaphoreGive(s_mutex);
            }
            if (have_credentials) {
                ha_instance_info_t info;
                if (ha_client_get_instance_info(url, token, &info)) {
                    ESP_LOGI(TAG, "home location resolved: %.4f, %.4f", info.latitude, info.longitude);
                    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                        s_lat = info.latitude;
                        s_lon = info.longitude;
                        s_have_location = true;
                        xSemaphoreGive(s_mutex);
                    }
                    have_location = true;
                    lat = info.latitude;
                    lon = info.longitude;
                    cloud_refresh_countdown = 0; /* fetch cloud cover on this same tick, don't wait another cycle */
                } else {
                    ESP_LOGW(TAG, "could not resolve home location from Home Assistant yet - will retry");
                }
            }
        }

        if (have_location && cloud_refresh_countdown <= 0) {
            cloud_refresh_countdown = CLOUD_REFRESH_TICKS;
            float pct;
            if (fetch_cloud_cover(lat, lon, &pct)) {
                if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    s_cloud_cover_pct = pct;
                    s_have_cloud = true;
                    xSemaphoreGive(s_mutex);
                }
            } else {
                ESP_LOGW(TAG, "open-meteo cloud cover fetch failed - will retry");
            }
        }

        if (cloud_refresh_countdown > 0) {
            cloud_refresh_countdown--;
        }

        vTaskDelay(pdMS_TO_TICKS(IRRADIANCE_POLL_INTERVAL_MS));
    }
}

void irradiance_model_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(irradiance_task, "irradiance_model", IRRADIANCE_TASK_STACK, NULL, IRRADIANCE_TASK_PRIORITY, NULL);
}

irradiance_reading_t irradiance_model_get(void)
{
    irradiance_reading_t out = {0};

    bool have_credentials = false, have_location = false, have_cloud = false;
    double lat = 0.0, lon = 0.0;
    float cloud_pct = 0.0f;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        have_credentials = s_have_ha_credentials;
        have_location = s_have_location;
        lat = s_lat;
        lon = s_lon;
        have_cloud = s_have_cloud;
        cloud_pct = s_cloud_cover_pct;
        xSemaphoreGive(s_mutex);
    }

    if (!have_credentials) {
        out.status = IRRADIANCE_NOT_CONFIGURED; /* Home Assistant isn't set up on this device at all yet */
        return out;
    }

    time_t now = time(NULL);
    bool clock_synced = now >= TIME_SANE_THRESHOLD;

    if (!have_location || !have_cloud || !clock_synced) {
        out.status = IRRADIANCE_WAITING; /* HA is set up, still resolving location/weather/clock */
        return out;
    }

    out.status = IRRADIANCE_LIVE;
    out.cloud_cover_pct = cloud_pct;
    out.wm2 = (float)sun_math_irradiance_wm2(now, lat, lon, (double)cloud_pct);
    return out;
}
