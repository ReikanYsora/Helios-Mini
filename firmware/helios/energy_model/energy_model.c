#include "energy_model.h"
#include "energy_config.h"
#include "ha_client.h"
#include "energy_rings.h"
#include "storage.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "energy_model";
/* Same namespace/keys networking/settings_server writes ha_url/ha_token
 * under - duplicated on purpose rather than pulling in a dependency on
 * settings_server (see energy_model.h). */
static const char *NVS_NAMESPACE = "helios_mini";

#define ENERGY_POLL_INTERVAL_MS    10000
#define ENERGY_TASK_STACK          4096
#define ENERGY_TASK_PRIORITY       3
#define ENERGY_ACTIVE_THRESHOLD_W  5.0f /* below this, treat import/export (or
                                          * charge/discharge) as "not really
                                          * happening" rather than flickering
                                          * between the two on noise */

/* Same colors as the parent Helios HA card's chip-appearance.ts. */
#define COLOR_SOLAR              0xff9800
#define COLOR_GRID_IMPORT        0x488fc2
#define COLOR_GRID_EXPORT        0x8353d1
#define COLOR_BATTERY_CHARGE     0xf06292
#define COLOR_BATTERY_DISCHARGE  0x4db6ac

static SemaphoreHandle_t s_status_mutex;
static SemaphoreHandle_t s_wake_sem;
static energy_model_status_t s_status;
static bool s_rings_shown = false;

static bool load_ha_credentials(char *url_out, size_t url_len, char *token_out, size_t token_len)
{
    url_out[0] = '\0';
    token_out[0] = '\0';
    bool have_url = storage_get_string(NVS_NAMESPACE, "ha_url", url_out, url_len) == ESP_OK && url_out[0] != '\0';
    bool have_token = storage_get_string(NVS_NAMESPACE, "ha_token", token_out, token_len) == ESP_OK && token_out[0] != '\0';
    return have_url && have_token;
}

static void poll_entity(const char *url, const char *token, const char *entity_id,
                         energy_entity_status_t *out)
{
    out->configured = (entity_id != NULL && entity_id[0] != '\0');
    out->last_value_w = 0.0f;
    if (!out->configured) {
        out->status = HA_ENTITY_STATUS_NOT_CONFIGURED;
        return;
    }
    out->status = ha_client_get_entity_power(url, token, entity_id, &out->last_value_w);
}

static float clamp_percent(float value_w, float max_w)
{
    if (max_w <= 0.0f) {
        return 0.0f;
    }
    float pct = (value_w / max_w) * 100.0f;
    if (pct < 0.0f) {
        pct = 0.0f;
    } else if (pct > 100.0f) {
        pct = 100.0f;
    }
    return pct;
}

static void format_watts(float watts, char *out, size_t out_len)
{
    if (watts >= 1000.0f || watts <= -1000.0f) {
        snprintf(out, out_len, "%.2f kW", watts / 1000.0f);
    } else {
        snprintf(out, out_len, "%.0f W", watts);
    }
}

static void energy_model_task(void *arg)
{
    (void)arg;
    while (1) {
        char url[128] = {0};
        char token[256] = {0};
        bool ha_configured = load_ha_credentials(url, sizeof(url), token, sizeof(token));
        const char *u = ha_configured ? url : NULL;
        const char *t = ha_configured ? token : NULL;

        energy_entity_config_t entities;
        energy_limits_t limits;
        energy_config_load_entities(&entities);
        energy_config_load_limits(&limits);

        energy_model_status_t status = {0};
        status.ha_configured = ha_configured;
        poll_entity(u, t, entities.solar, &status.solar);
        poll_entity(u, t, entities.grid_import, &status.grid_import);
        poll_entity(u, t, entities.grid_export, &status.grid_export);
        poll_entity(u, t, entities.battery_charge, &status.battery_charge);
        poll_entity(u, t, entities.battery_discharge, &status.battery_discharge);
        poll_entity(u, t, entities.home, &status.home);
        status.polled_once = true;

        ESP_LOGD(TAG, "poll: solar=%d grid_in=%d grid_out=%d bat_chg=%d bat_dis=%d home=%d",
                  status.solar.status, status.grid_import.status, status.grid_export.status,
                  status.battery_charge.status, status.battery_discharge.status, status.home.status);

        /* publish a snapshot for the /display status panel */
        if (s_status_mutex != NULL && xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            s_status = status;
            xSemaphoreGive(s_status_mutex);
        }

        bool solar_ok = status.solar.configured && status.solar.status == HA_ENTITY_STATUS_OK;
        bool import_ok = status.grid_import.configured && status.grid_import.status == HA_ENTITY_STATUS_OK;
        bool export_ok = status.grid_export.configured && status.grid_export.status == HA_ENTITY_STATUS_OK;
        bool charge_ok = status.battery_charge.configured && status.battery_charge.status == HA_ENTITY_STATUS_OK;
        bool discharge_ok = status.battery_discharge.configured && status.battery_discharge.status == HA_ENTITY_STATUS_OK;
        bool home_ok = status.home.configured && status.home.status == HA_ENTITY_STATUS_OK;

        energy_display_t display = {0};

        display.solar.visible = status.solar.configured;
        display.solar.color_hex = COLOR_SOLAR;
        display.solar.percent = solar_ok ? clamp_percent(status.solar.last_value_w, limits.max_solar_w) : 0.0f;

        float export_w = export_ok ? status.grid_export.last_value_w : 0.0f;
        float import_w = import_ok ? status.grid_import.last_value_w : 0.0f;
        display.grid.visible = status.grid_import.configured || status.grid_export.configured;
        if (export_w > import_w && export_w > ENERGY_ACTIVE_THRESHOLD_W) {
            display.grid.color_hex = COLOR_GRID_EXPORT;
            display.grid.percent = clamp_percent(export_w, limits.max_grid_export_w);
        } else {
            display.grid.color_hex = COLOR_GRID_IMPORT;
            display.grid.percent = clamp_percent(import_w, limits.max_grid_import_w);
        }

        float charge_w = charge_ok ? status.battery_charge.last_value_w : 0.0f;
        float discharge_w = discharge_ok ? status.battery_discharge.last_value_w : 0.0f;
        display.battery.visible = status.battery_charge.configured || status.battery_discharge.configured;
        if (charge_w > discharge_w && charge_w > ENERGY_ACTIVE_THRESHOLD_W) {
            display.battery.color_hex = COLOR_BATTERY_CHARGE;
            display.battery.percent = clamp_percent(charge_w, limits.max_battery_w);
        } else {
            display.battery.color_hex = COLOR_BATTERY_DISCHARGE;
            display.battery.percent = clamp_percent(discharge_w, limits.max_battery_w);
        }

        if (home_ok) {
            format_watts(status.home.last_value_w, display.center_text, sizeof(display.center_text));
            strncpy(display.center_sub, "home consumption", sizeof(display.center_sub) - 1);
        } else if (!status.home.configured) {
            strncpy(display.center_text, "--", sizeof(display.center_text) - 1);
            strncpy(display.center_sub, "home entity not set", sizeof(display.center_sub) - 1);
        } else if (!ha_configured) {
            strncpy(display.center_text, "--", sizeof(display.center_text) - 1);
            strncpy(display.center_sub, "Home Assistant not set up", sizeof(display.center_sub) - 1);
        } else {
            strncpy(display.center_text, "--", sizeof(display.center_text) - 1);
            strncpy(display.center_sub, ha_entity_status_text(status.home.status), sizeof(display.center_sub) - 1);
        }

        /* Switch from the persistent IP notice to the rings the first time
         * there's actually something to show - the home entity is what
         * gates it since the center text is never blank once it's set. */
        if (status.home.configured && !s_rings_shown) {
            energy_rings_show();
            s_rings_shown = true;
        }
        if (s_rings_shown) {
            energy_rings_update(&display);
        }

        if (s_wake_sem != NULL) {
            /* Blocks for the poll interval, unless energy_model_refresh_config()
             * wakes it early. */
            xSemaphoreTake(s_wake_sem, pdMS_TO_TICKS(ENERGY_POLL_INTERVAL_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(ENERGY_POLL_INTERVAL_MS));
        }
    }
}

void energy_model_start(void)
{
    s_status_mutex = xSemaphoreCreateMutex();
    s_wake_sem = xSemaphoreCreateBinary();
    xTaskCreate(energy_model_task, "energy_model", ENERGY_TASK_STACK, NULL, ENERGY_TASK_PRIORITY, NULL);
}

void energy_model_refresh_config(void)
{
    if (s_wake_sem != NULL) {
        xSemaphoreGive(s_wake_sem);
    }
}

void energy_model_get_status(energy_model_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_status_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        *out = s_status;
        xSemaphoreGive(s_status_mutex);
    }
}
