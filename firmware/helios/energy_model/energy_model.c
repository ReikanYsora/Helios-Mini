#include "energy_model.h"
#include "energy_config.h"
#include "ha_ws.h"
#include "energy_rings.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

#define ENERGY_RENDER_INTERVAL_MS 3000
/* ha_ws_status_t carries 5 rings' worth of (possibly multi-entity,
 * comma-joined) source ids - a couple KB by itself once declared as a
 * local in render() below, on top of energy_display_t/energy_limits_t and
 * the call chain into ha_ws_get_status()/energy_rings_update(). Sized
 * with real headroom after a stack overflow at a smaller value on actual
 * hardware - see docs/HARDWARE_REFERENCE.md. */
#define ENERGY_TASK_STACK         6144
#define ENERGY_TASK_PRIORITY      3
#define ENERGY_ACTIVE_THRESHOLD_W 5.0f /* below this, treat import/export (or charge/discharge)
                                         * as "not really happening" rather than flickering
                                         * between the two on noise */

/* Same colors as the parent Helios HA card's chip-appearance.ts. */
#define COLOR_SOLAR              0xff9800
#define COLOR_GRID_IMPORT        0x488fc2
#define COLOR_GRID_EXPORT        0x8353d1
#define COLOR_BATTERY_CHARGE     0xf06292
#define COLOR_BATTERY_DISCHARGE  0x4db6ac

static SemaphoreHandle_t s_wake_sem;
static bool s_rings_shown = false;

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

static bool source_has_value(const ha_ws_source_t *s)
{
    return s->status == HA_WS_SOURCE_LIVE || s->status == HA_WS_SOURCE_STALE;
}

static void render(void)
{
    ha_ws_status_t ws;
    ha_ws_get_status(&ws);

    if (!ws.energy_dashboard_configured) {
        return; /* nothing to show yet - leave the IP notice on screen */
    }
    if (!s_rings_shown) {
        energy_rings_show();
        s_rings_shown = true;
    }

    energy_limits_t limits;
    energy_config_load_limits(&limits);
    energy_format_t format;
    energy_config_load_format(&format);

    energy_display_t display = {0};

    display.solar.visible = ws.solar.status != HA_WS_SOURCE_NOT_CONFIGURED;
    display.solar.color_hex = COLOR_SOLAR;
    display.solar.percent = source_has_value(&ws.solar) ? clamp_percent(ws.solar.power_w, limits.max_solar_w) : 0.0f;

    float import_w = source_has_value(&ws.grid_import) ? ws.grid_import.power_w : 0.0f;
    float export_w = source_has_value(&ws.grid_export) ? ws.grid_export.power_w : 0.0f;
    display.grid.visible = ws.grid_import.status != HA_WS_SOURCE_NOT_CONFIGURED ||
                            ws.grid_export.status != HA_WS_SOURCE_NOT_CONFIGURED;
    if (export_w > import_w && export_w > ENERGY_ACTIVE_THRESHOLD_W) {
        display.grid.color_hex = COLOR_GRID_EXPORT;
        display.grid.percent = clamp_percent(export_w, limits.max_grid_export_w);
    } else {
        display.grid.color_hex = COLOR_GRID_IMPORT;
        display.grid.percent = clamp_percent(import_w, limits.max_grid_import_w);
    }

    float charge_w = source_has_value(&ws.battery_charge) ? ws.battery_charge.power_w : 0.0f;
    float discharge_w = source_has_value(&ws.battery_discharge) ? ws.battery_discharge.power_w : 0.0f;
    display.battery.visible = ws.battery_charge.status != HA_WS_SOURCE_NOT_CONFIGURED ||
                               ws.battery_discharge.status != HA_WS_SOURCE_NOT_CONFIGURED;
    if (charge_w > discharge_w && charge_w > ENERGY_ACTIVE_THRESHOLD_W) {
        display.battery.color_hex = COLOR_BATTERY_CHARGE;
        display.battery.percent = clamp_percent(charge_w, limits.max_battery_w);
    } else {
        display.battery.color_hex = COLOR_BATTERY_DISCHARGE;
        display.battery.percent = clamp_percent(discharge_w, limits.max_battery_w);
    }

    /* Home Assistant's Energy Dashboard has no single "home total
     * consumption" entity to discover - Helios's own card computes it
     * from the same measured values instead (src/core/energy.ts,
     * consumptionLoad()): load = production + gridImport - gridExport -
     * netBattery, netBattery = charge - discharge (charge positive),
     * clamped at 0. Mirrored here exactly rather than reinvented. */
    bool have_home_signal = ws.solar.status != HA_WS_SOURCE_NOT_CONFIGURED ||
                             ws.grid_import.status != HA_WS_SOURCE_NOT_CONFIGURED ||
                             ws.grid_export.status != HA_WS_SOURCE_NOT_CONFIGURED ||
                             ws.battery_charge.status != HA_WS_SOURCE_NOT_CONFIGURED ||
                             ws.battery_discharge.status != HA_WS_SOURCE_NOT_CONFIGURED;
    if (have_home_signal) {
        float solar_w = source_has_value(&ws.solar) ? ws.solar.power_w : 0.0f;
        float net_battery_w = charge_w - discharge_w;
        float home_w = solar_w + import_w - export_w - net_battery_w;
        if (home_w < 0.0f) {
            home_w = 0.0f;
        }
        energy_format_power(home_w, &format, display.center_text, sizeof(display.center_text));
        /* center_sub intentionally left empty - the house icon above the
         * number is the "this is home consumption" label now, ui/home
         * doesn't need a redundant caption when everything's fine. */
    } else {
        strncpy(display.center_text, "--", sizeof(display.center_text) - 1);
        strncpy(display.center_sub, "no source in your Energy Dashboard",
                sizeof(display.center_sub) - 1);
    }

    energy_rings_update(&display);
}

static void energy_model_task(void *arg)
{
    (void)arg;
    ha_ws_start();
    for (;;) {
        render();
        xSemaphoreTake(s_wake_sem, pdMS_TO_TICKS(ENERGY_RENDER_INTERVAL_MS));
    }
}

void energy_model_start(void)
{
    s_wake_sem = xSemaphoreCreateBinary();
    xTaskCreate(energy_model_task, "energy_model", ENERGY_TASK_STACK, NULL, ENERGY_TASK_PRIORITY, NULL);
}

void energy_model_refresh(void)
{
    if (s_wake_sem != NULL) {
        xSemaphoreGive(s_wake_sem);
    }
}
