#include "energy_model.h"
#include "energy_config.h"
#include "ha_ws.h"
#include "energy_rings.h"
#include "irradiance_model.h"
#include "wifi_sta.h"

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
#define COLOR_IRRADIANCE         0xffc107
#define COLOR_GRID_IMPORT        0x488fc2
#define COLOR_GRID_EXPORT        0x8353d1
#define COLOR_BATTERY_CHARGE     0xf06292
#define COLOR_BATTERY_DISCHARGE  0x4db6ac
/* Irradiance's 0-100% reference: 1000 W/m^2 is Standard Test Conditions
 * (STC) peak sun, the same denominator Helios's own computePvPower() uses
 * (irradiance/10) - a physical constant, not a per-system capacity, so
 * unlike the solar/grid/battery rings there's no user-configurable max. */
#define IRRADIANCE_STC_WM2       1000.0f

static SemaphoreHandle_t s_wake_sem;
static bool s_rings_shown = false;
/* Last page-visibility config the screen was actually built with - so a
 * /display toggle change (energy_model_refresh() wakes this task early)
 * gets picked up as a rebuild without tearing the whole screen down and
 * rebuilding it every 3s render tick for no reason. */
static energy_page_visibility_t s_shown_pages;

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

static energy_ring_status_t map_status(ha_ws_source_status_t s)
{
    switch (s) {
        case HA_WS_SOURCE_WAITING: return ENERGY_RING_WAITING;
        case HA_WS_SOURCE_LIVE:    return ENERGY_RING_LIVE;
        case HA_WS_SOURCE_STALE:   return ENERGY_RING_STALE;
        default:                   return ENERGY_RING_NOT_CONFIGURED;
    }
}

static energy_ring_status_t map_irradiance_status(irradiance_status_t s)
{
    switch (s) {
        case IRRADIANCE_WAITING: return ENERGY_RING_WAITING;
        case IRRADIANCE_LIVE:    return ENERGY_RING_LIVE; /* no STALE concept here - every call recomputes live */
        default:                 return ENERGY_RING_NOT_CONFIGURED;
    }
}

/* Fills in a breakdown_* line ("Solar  862 W") for the consumption view -
 * left empty (and hidden by ui/home) unless this contributor actually has
 * a live reading right now. */
static void set_breakdown(char *out, size_t out_len, const char *label, bool live, float watts,
                           const energy_format_t *format)
{
    out[0] = '\0';
    if (!live) {
        return;
    }
    char value[24];
    energy_format_power(watts, format, value, sizeof(value));
    snprintf(out, out_len, "%s  %s", label, value);
}

static void render(void)
{
    ha_ws_status_t ws;
    ha_ws_get_status(&ws);

    if (!ws.energy_dashboard_configured) {
        return; /* nothing to show yet - leave the IP notice on screen */
    }

    energy_page_visibility_t pages;
    energy_config_load_pages(&pages);
    bool pages_changed = memcmp(&pages, &s_shown_pages, sizeof(pages)) != 0;
    if (!s_rings_shown || pages_changed) {
        energy_pages_t ui_pages = {
            .solar = pages.show_solar,
            .irradiance = pages.show_irradiance,
            .grid = pages.show_grid,
            .battery = pages.show_battery,
            .consumption = pages.show_consumption,
        };
        energy_rings_show(&ui_pages);
        s_shown_pages = pages;
        s_rings_shown = true;
    }

    /* Keeps the swipe-up IP screen fresh every tick rather than relying
     * solely on wifi_sta's connected-callback: that callback can (and
     * usually does, since Home Assistant/Energy Dashboard discovery takes
     * a few seconds) fire before energy_rings_show() has even built the
     * IP tile, in which case main.c's forwarded call is a silent no-op
     * and nothing would ever populate it again until the next reconnect. */
    char ip[16];
    energy_rings_set_ip(wifi_sta_get_ip(ip, sizeof(ip)) ? ip : NULL);

    energy_limits_t limits;
    energy_config_load_limits(&limits);
    energy_format_t format;
    energy_config_load_format(&format);

    energy_display_t display = {0};

    display.solar.visible = ws.solar.status != HA_WS_SOURCE_NOT_CONFIGURED;
    display.solar.status = map_status(ws.solar.status);
    display.solar.color_hex = COLOR_SOLAR;
    bool solar_live = source_has_value(&ws.solar);
    display.solar.percent = solar_live ? clamp_percent(ws.solar.power_w, limits.max_solar_w) : 0.0f;
    if (solar_live) {
        energy_format_power(ws.solar.power_w, &format, display.solar.value_text, sizeof(display.solar.value_text));
    }

    /* Ground-horizontal irradiance: independent of solar.status above (and
     * of the Energy Dashboard entirely) - helios/irradiance_model works
     * from Home Assistant's own location config plus Open-Meteo, not an
     * HA entity, so it has its own NOT_CONFIGURED/WAITING/LIVE lifecycle. */
    irradiance_reading_t irr = irradiance_model_get();
    display.irradiance.visible = irr.status != IRRADIANCE_NOT_CONFIGURED;
    display.irradiance.status = map_irradiance_status(irr.status);
    display.irradiance.color_hex = COLOR_IRRADIANCE;
    display.irradiance.percent = (irr.status == IRRADIANCE_LIVE) ? clamp_percent(irr.wm2, IRRADIANCE_STC_WM2) : 0.0f;
    if (irr.status == IRRADIANCE_LIVE) {
        snprintf(display.irradiance.value_text, sizeof(display.irradiance.value_text), "%.0f W/m2", (double)irr.wm2);
        snprintf(display.irradiance.direction_label, sizeof(display.irradiance.direction_label), "%.0f%% cloud",
                 (double)irr.cloud_cover_pct);
    }

    /* Grid and battery each pick whichever direction is actually
     * configured; when both are, the one with the bigger (above-noise)
     * reading wins, so the ring/hero view always reflects what's really
     * happening rather than defaulting to "import"/"discharge" out of
     * pure convention. */
    bool import_configured = ws.grid_import.status != HA_WS_SOURCE_NOT_CONFIGURED;
    bool export_configured = ws.grid_export.status != HA_WS_SOURCE_NOT_CONFIGURED;
    float import_w = source_has_value(&ws.grid_import) ? ws.grid_import.power_w : 0.0f;
    float export_w = source_has_value(&ws.grid_export) ? ws.grid_export.power_w : 0.0f;
    display.grid.visible = import_configured || export_configured;

    bool use_export = (import_configured && export_configured)
                           ? (export_w > import_w && export_w > ENERGY_ACTIVE_THRESHOLD_W)
                           : export_configured;
    if (use_export) {
        display.grid.color_hex = COLOR_GRID_EXPORT;
        display.grid.percent = clamp_percent(export_w, limits.max_grid_export_w);
        display.grid.status = map_status(ws.grid_export.status);
        strncpy(display.grid.direction_label, "Export", sizeof(display.grid.direction_label) - 1);
        if (source_has_value(&ws.grid_export)) {
            energy_format_power(export_w, &format, display.grid.value_text, sizeof(display.grid.value_text));
        }
    } else {
        display.grid.color_hex = COLOR_GRID_IMPORT;
        display.grid.percent = clamp_percent(import_w, limits.max_grid_import_w);
        display.grid.status = map_status(ws.grid_import.status);
        strncpy(display.grid.direction_label, "Import", sizeof(display.grid.direction_label) - 1);
        if (source_has_value(&ws.grid_import)) {
            energy_format_power(import_w, &format, display.grid.value_text, sizeof(display.grid.value_text));
        }
    }

    bool charge_configured = ws.battery_charge.status != HA_WS_SOURCE_NOT_CONFIGURED;
    bool discharge_configured = ws.battery_discharge.status != HA_WS_SOURCE_NOT_CONFIGURED;
    float charge_w = source_has_value(&ws.battery_charge) ? ws.battery_charge.power_w : 0.0f;
    float discharge_w = source_has_value(&ws.battery_discharge) ? ws.battery_discharge.power_w : 0.0f;
    display.battery.visible = charge_configured || discharge_configured;

    bool use_charge = (charge_configured && discharge_configured)
                           ? (charge_w > discharge_w && charge_w > ENERGY_ACTIVE_THRESHOLD_W)
                           : charge_configured;
    if (use_charge) {
        display.battery.color_hex = COLOR_BATTERY_CHARGE;
        display.battery.percent = clamp_percent(charge_w, limits.max_battery_w);
        display.battery.status = map_status(ws.battery_charge.status);
        strncpy(display.battery.direction_label, "Charge", sizeof(display.battery.direction_label) - 1);
        if (source_has_value(&ws.battery_charge)) {
            energy_format_power(charge_w, &format, display.battery.value_text, sizeof(display.battery.value_text));
        }
    } else {
        display.battery.color_hex = COLOR_BATTERY_DISCHARGE;
        display.battery.percent = clamp_percent(discharge_w, limits.max_battery_w);
        display.battery.status = map_status(ws.battery_discharge.status);
        strncpy(display.battery.direction_label, "Discharge", sizeof(display.battery.direction_label) - 1);
        if (source_has_value(&ws.battery_discharge)) {
            energy_format_power(discharge_w, &format, display.battery.value_text, sizeof(display.battery.value_text));
        }
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
        float solar_w = solar_live ? ws.solar.power_w : 0.0f;
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

    /* Every contributor to the formula above, spelled out - the one thing
     * the dedicated consumption page shows that the compact center text
     * can't. All independent of which grid/battery direction "won" above:
     * the formula itself uses every configured term, so the breakdown
     * does too. */
    set_breakdown(display.breakdown_solar, sizeof(display.breakdown_solar), "Solar", solar_live, ws.solar.power_w, &format);
    set_breakdown(display.breakdown_grid_import, sizeof(display.breakdown_grid_import), "Grid import",
                  source_has_value(&ws.grid_import), import_w, &format);
    set_breakdown(display.breakdown_grid_export, sizeof(display.breakdown_grid_export), "Grid export",
                  source_has_value(&ws.grid_export), export_w, &format);
    set_breakdown(display.breakdown_battery_charge, sizeof(display.breakdown_battery_charge), "Battery charge",
                  source_has_value(&ws.battery_charge), charge_w, &format);
    set_breakdown(display.breakdown_battery_discharge, sizeof(display.breakdown_battery_discharge), "Battery discharge",
                  source_has_value(&ws.battery_discharge), discharge_w, &format);

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
