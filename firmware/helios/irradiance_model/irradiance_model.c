#include "irradiance_model.h"
#include "sun_math.h"
#include "ha_client.h"
#include "helios_config.h"
#include "energy_math.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "irradiance_model";

#define IRRADIANCE_HA_URL_LEN   192
#define IRRADIANCE_HA_TOKEN_LEN 256

#define IRRADIANCE_TASK_STACK    6144
#define IRRADIANCE_TASK_PRIORITY 3
#define IRRADIANCE_POLL_INTERVAL_MS (60 * 1000)

/* One Open-Meteo fetch returns a 48h hourly forecast; we keep all of it and
 * serve the current hour from memory, refetching only when the cache no
 * longer covers "now" or has aged past this (to pick up updated forecasts),
 * instead of re-downloading it every few minutes. */
#define FORECAST_MAX_HOURS   48
#define FORECAST_TIME_LEN    17   /* "YYYY-MM-DDTHH:00" + NUL */
#define FORECAST_MAX_AGE_US  (3LL * 3600 * 1000000)

/* The sun barely moves in a minute; recompute the irradiance at most this
 * often rather than on every get() (called every render tick). */
#define IRRADIANCE_COMPUTE_TTL_US (60LL * 1000000)

/* A device clock that hasn't SNTP-synced yet reads near the epoch; computing
 * sun position from that would show wrong data as if it were live. Treat
 * anything before 2024 as "not synced yet". */
#define TIME_SANE_THRESHOLD ((time_t)1704067200)

static SemaphoreHandle_t s_mutex; /* guards every s_* field below */
static bool s_have_ha_credentials = false;
static bool s_have_location = false;
static double s_lat = 0.0;
static double s_lon = 0.0;

/* Cached Open-Meteo hourly forecast (effective cloud per hour) plus the
 * current hour's value pulled from it. */
static char s_fc_time[FORECAST_MAX_HOURS][FORECAST_TIME_LEN];
static float s_fc_cloud[FORECAST_MAX_HOURS];
static int s_fc_count = 0;
static int64_t s_fc_fetched_us = 0;
static bool s_have_cloud = false;
static float s_cloud_cover_pct = 0.0f;

/* Cached irradiance result, so repeated get() calls within the TTL reuse it
 * instead of rerunning the sun math. */
static int64_t s_irr_computed_us = 0;
static float s_irr_wm2 = 0.0f;
static float s_irr_cloud_used = -1.0f;

/* "YYYY-MM-DDTHH:00" for the current UTC hour - the key Open-Meteo's
 * timezone=UTC slots use. Wide buffer: GCC's format-truncation checker
 * can't bound %04d/%02d against the runtime range of tm fields. */
static void now_hour_string(char *out, size_t len)
{
    time_t now = time(NULL);
    struct tm tm_now;
    gmtime_r(&now, &tm_now);
    snprintf(out, len, "%04d-%02d-%02dT%02d:00",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour);
}

/* Fetches Open-Meteo's hourly cloud_cover_low/mid/high and blends each hour
 * the way Helios's own fetchHomePointData() does (helios_effective_cloud):
 * this beats the API's raw total cloud_cover, which over-counts high cirrus
 * on an otherwise clear day. Fills the caller's arrays; touches no shared
 * state, so the slow HTTP call runs without holding s_mutex. */
static bool fetch_forecast(double lat, double lon, char time_out[][FORECAST_TIME_LEN],
                           float *cloud_out, int max, int *count_out)
{
    *count_out = 0;

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&hourly=cloud_cover_low,cloud_cover_mid,cloud_cover_high&forecast_days=2&timezone=UTC",
             lat, lon);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach, /* HTTPS public internet, not HA's own (often plain-HTTP LAN) client */
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
                int n = cJSON_GetArraySize(times);
                int count = 0;
                for (int i = 0; i < n && count < max; i++) {
                    cJSON *t = cJSON_GetArrayItem(times, i);
                    if (!cJSON_IsString(t) || t->valuestring == NULL ||
                        strlen(t->valuestring) >= FORECAST_TIME_LEN) {
                        continue;
                    }
                    cJSON *lo = cJSON_GetArrayItem(low, i);
                    cJSON *mi = cJSON_GetArrayItem(mid, i);
                    cJSON *hi = cJSON_GetArrayItem(high, i);
                    float lc = cJSON_IsNumber(lo) ? (float)lo->valuedouble : 0.0f;
                    float mc = cJSON_IsNumber(mi) ? (float)mi->valuedouble : 0.0f;
                    float hc = cJSON_IsNumber(hi) ? (float)hi->valuedouble : 0.0f;
                    strncpy(time_out[count], t->valuestring, FORECAST_TIME_LEN - 1);
                    time_out[count][FORECAST_TIME_LEN - 1] = '\0';
                    cloud_out[count] = helios_effective_cloud(lc, mc, hc);
                    count++;
                }
                *count_out = count;
                ok = count > 0;
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

/* Current hour's cached cloud, or -1 if the forecast doesn't cover now.
 * Caller holds s_mutex. */
static float cached_cloud_now_locked(void)
{
    char want[64];
    now_hour_string(want, sizeof(want));
    for (int i = 0; i < s_fc_count; i++) {
        if (strcmp(s_fc_time[i], want) == 0) {
            return s_fc_cloud[i];
        }
    }
    return -1.0f;
}

static void resolve_location_once(double *lat, double *lon, bool *have_location)
{
    char url[IRRADIANCE_HA_URL_LEN] = {0};
    char token[IRRADIANCE_HA_TOKEN_LEN] = {0};
    bool have_credentials = helios_config_get_ha_credentials(url, sizeof(url), token, sizeof(token));
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_have_ha_credentials = have_credentials;
        xSemaphoreGive(s_mutex);
    }
    if (!have_credentials) {
        return;
    }

    ha_instance_info_t info;
    if (!ha_client_get_instance_info(url, token, &info)) {
        ESP_LOGW(TAG, "could not resolve home location from Home Assistant yet - will retry");
        return;
    }
    ESP_LOGI(TAG, "home location resolved: %.4f, %.4f", info.latitude, info.longitude);
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_lat = info.latitude;
        s_lon = info.longitude;
        s_have_location = true;
        xSemaphoreGive(s_mutex);
    }
    *lat = info.latitude;
    *lon = info.longitude;
    *have_location = true;
}

/* Refreshes the cached forecast if it no longer covers the current hour or
 * has aged out, then pins the current hour's cloud value. The HTTP fetch
 * runs into a scratch buffer without the lock; only the copy-in is locked. */
static void refresh_cloud(double lat, double lon)
{
    bool need_fetch;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool covered = cached_cloud_now_locked() >= 0.0f;
        bool aged = (s_fc_count == 0) ||
                    (esp_timer_get_time() - s_fc_fetched_us > FORECAST_MAX_AGE_US);
        need_fetch = !covered || aged;
        xSemaphoreGive(s_mutex);
    } else {
        return;
    }

    if (need_fetch) {
        /* Static, not stack: the two arrays are ~1KB, too much for this
         * task's stack (see docs/HARDWARE_REFERENCE.md's stack-overflow
         * history); one task owns this so a shared scratch is safe. */
        static char fc_time[FORECAST_MAX_HOURS][FORECAST_TIME_LEN];
        static float fc_cloud[FORECAST_MAX_HOURS];
        int fc_count = 0;
        if (fetch_forecast(lat, lon, fc_time, fc_cloud, FORECAST_MAX_HOURS, &fc_count)) {
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                memcpy(s_fc_time, fc_time, sizeof(s_fc_time));
                memcpy(s_fc_cloud, fc_cloud, sizeof(s_fc_cloud));
                s_fc_count = fc_count;
                s_fc_fetched_us = esp_timer_get_time();
                xSemaphoreGive(s_mutex);
            }
        } else {
            ESP_LOGW(TAG, "open-meteo forecast fetch failed - will retry");
        }
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        float c = cached_cloud_now_locked();
        if (c >= 0.0f) {
            s_cloud_cover_pct = c;
            s_have_cloud = true;
        }
        xSemaphoreGive(s_mutex);
    }
}

static void irradiance_task(void *arg)
{
    (void)arg;

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
            resolve_location_once(&lat, &lon, &have_location);
        }

        /* Cloud/forecast needs a real clock (its slots are keyed by the
         * current UTC hour); skip until SNTP has synced rather than fetch
         * against a 1970 timestamp. */
        if (have_location && time(NULL) >= TIME_SANE_THRESHOLD) {
            refresh_cloud(lat, lon);
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
        out.status = IRRADIANCE_NOT_CONFIGURED;
        return out;
    }

    time_t now = time(NULL);
    if (!have_location || !have_cloud || now < TIME_SANE_THRESHOLD) {
        out.status = IRRADIANCE_WAITING;
        return out;
    }

    out.status = IRRADIANCE_LIVE;
    out.cloud_cover_pct = cloud_pct;

    /* Reuse the last computed value within the TTL unless the cloud input
     * changed - the sun barely moves in a minute. */
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        int64_t now_us = esp_timer_get_time();
        bool fresh = (s_irr_cloud_used == cloud_pct) &&
                     (now_us - s_irr_computed_us < IRRADIANCE_COMPUTE_TTL_US);
        if (!fresh) {
            s_irr_wm2 = (float)sun_math_irradiance_wm2(now, lat, lon, (double)cloud_pct);
            s_irr_cloud_used = cloud_pct;
            s_irr_computed_us = now_us;
        }
        out.wm2 = s_irr_wm2;
        xSemaphoreGive(s_mutex);
    } else {
        out.wm2 = (float)sun_math_irradiance_wm2(now, lat, lon, (double)cloud_pct);
    }

    return out;
}
