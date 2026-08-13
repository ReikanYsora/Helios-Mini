#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Pure LVGL renderer for the energy screens: an Apple-Watch-style
 * four-ring general view plus five dedicated per-source views (solar,
 * irradiance, grid, battery, home consumption), swipeable via an
 * lv_tileview. Knows nothing about Home Assistant, Open-Meteo or entity
 * configuration - that's helios/energy_model (and, for irradiance,
 * helios/irradiance_model), which polls the data, decides what every
 * string and status means, and calls energy_rings_update(). Kept dumb on
 * purpose so this component only ever REQUIRES display. */

typedef enum {
    ENERGY_RING_NOT_CONFIGURED, /* nothing to show - no source for this ring at all */
    ENERGY_RING_WAITING,        /* configured, but no live reading yet */
    ENERGY_RING_LIVE,           /* has a current reading */
    ENERGY_RING_STALE,          /* has a reading, but it's old */
} energy_ring_status_t;

typedef struct {
    bool visible;        /* false = ring not drawn on the general view - the
                           * underlying source isn't configured, so there's
                           * nothing to show instead of a misleading empty ring. */
    energy_ring_status_t status;
    float percent;        /* 0-100 against the configured max, already clamped. */
    uint32_t color_hex;   /* caller picks the color, e.g. grid ring is blue
                            * while importing and purple while exporting. */
    char value_text[24];  /* pre-formatted current reading, e.g. "862 W" -
                            * "" unless status is LIVE or STALE. */
    char direction_label[16]; /* a short caption under the hero chip - "Import" /
                                * "Export" / "Charge" / "Discharge" for the
                                * bidirectional rings, "42% cloud" for irradiance,
                                * "" for sources with only one direction/reading or
                                * nothing configured. */
} energy_ring_value_t;

typedef struct {
    energy_ring_value_t solar;      /* PV production vs. configured max. */
    energy_ring_value_t irradiance; /* ground-horizontal W/m^2 vs. 1000 (STC) - independent of any PV system. */
    energy_ring_value_t grid;       /* import OR export - one arc, whichever
                                      * direction is currently active. */
    energy_ring_value_t battery;    /* charge OR discharge - same idea. */
    char center_text[32];         /* e.g. "1.24 kW" - home consumption. */
    char center_sub[32];          /* small caption under the number, e.g.
                                    * an error hint when nothing is
                                    * configured yet - "" when there's
                                    * nothing to say (the house icon next
                                    * to it is label enough). */

    /* Home consumption breakdown for the dedicated consumption view - one
     * pre-formatted line per contributor, "" if that source isn't
     * configured/contributing right now. */
    char breakdown_solar[48];
    char breakdown_grid_import[48];
    char breakdown_grid_export[48];
    char breakdown_battery_charge[48];
    char breakdown_battery_discharge[48];
} energy_display_t;

/* Which of the five dedicated pages to build - the general view is always
 * shown, it's the main dashboard, not something to hide. Decoupled from
 * helios/energy_config's own NVS-backed struct (same reason
 * energy_ring_status_t is decoupled from ha_ws_source_status_t): this
 * component only ever REQUIRES display, energy_model.c does the mapping. */
typedef struct {
    bool solar;
    bool irradiance;
    bool grid;
    bool battery;
    bool consumption;
} energy_pages_t;

/* Builds the swipeable energy screens (general view + whichever of the 5
 * dedicated views *pages* enables, all default true) and shows them
 * (replacing whatever else is on screen - boot animation, the IP notice,
 * a previous call's screen, ...). Pass NULL for the pre-configurability
 * default (every page shown). Everything starts empty until the first
 * energy_rings_update(). Safe to call more than once - e.g. to rebuild
 * with a new page selection after a /display settings change. */
void energy_rings_show(const energy_pages_t *pages);

/* Updates every view in place from the same snapshot - cheap, meant to be
 * called every time helios/energy_model polls fresh data, regardless of
 * which page is currently on screen. No-op if energy_rings_show() hasn't
 * run yet. */
void energy_rings_update(const energy_display_t *display);

/* Updates the IP screen (swipe up from the general view) - ip is the
 * dotted-decimal station address ("192.168.0.45"), or NULL/"" while Wi-Fi
 * has no lease yet. No-op if energy_rings_show() hasn't run yet. */
void energy_rings_set_ip(const char *ip);
