#pragma once

#include <stdbool.h>
#include <stddef.h>

/* NVS-backed configuration for the energy rings' 100% power references.
 * Which Home Assistant entity feeds each ring is no longer configured
 * here (or anywhere in Helios Mini) - it's auto-discovered from Home
 * Assistant's own Energy Dashboard configuration by helios/ha_ws. Ring
 * limits stay a Helios Mini display preference with no HA equivalent to
 * discover, so they're still set on /display and stored here. */

typedef struct {
    float max_solar_w;
    float max_grid_import_w;
    float max_grid_export_w;
    float max_battery_w;
} energy_limits_t;

/* Defaults for a fresh device. max_solar_w mirrors Helios's own
 * DEFAULT_MAX_EXPECTED_POWER_W. */
#define ENERGY_DEFAULT_MAX_SOLAR_W       5000.0f
#define ENERGY_DEFAULT_MAX_GRID_IMPORT_W 5000.0f
#define ENERGY_DEFAULT_MAX_GRID_EXPORT_W 5000.0f
#define ENERGY_DEFAULT_MAX_BATTERY_W     3000.0f

/* Reads stored limits, falling back to the ENERGY_DEFAULT_* constants
 * above for anything never saved (or saved as an invalid/non-positive
 * number). */
void energy_config_load_limits(energy_limits_t *out);
void energy_config_save_limits(const energy_limits_t *limits);

/* How every power number on the device is displayed - the rings' center
 * text and the /display status panel both go through
 * energy_format_power() below, so there's exactly one place this is
 * decided. */
typedef struct {
    bool use_kw;  /* false = show W (default), true = show kW */
    int decimals; /* 0-3, default 1 */
} energy_format_t;

#define ENERGY_DEFAULT_USE_KW   false
#define ENERGY_DEFAULT_DECIMALS 1

void energy_config_load_format(energy_format_t *out);
void energy_config_save_format(const energy_format_t *format);

/* Formats `watts` per the given preference (or NULL for the current
 * stored/default one) into `out`, e.g. "1.2 kW" or "123 W". */
void energy_format_power(float watts, const energy_format_t *format, char *out, size_t out_len);
