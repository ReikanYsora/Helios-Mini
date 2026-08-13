#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Pure energy/irradiance math shared by the firmware and the host tests -
 * no ESP-IDF, no I/O. Mirrors the Helios card's own formulas
 * (src/core/energy.ts). Kept dependency-free so test/host can compile it
 * straight against a desktop toolchain. */

/* Value as a percentage of max, clamped to 0..100. 0 when max <= 0. */
float helios_clamp_percent(float value_w, float max_w);

/* Home consumption, exactly Helios's consumptionLoad(): production +
 * grid_import - grid_export - (battery_charge - battery_discharge),
 * clamped at 0. */
float helios_consumption_load(float production_w, float grid_import_w, float grid_export_w,
                              float battery_charge_w, float battery_discharge_w);

/* Converts a reading to watts from its Home Assistant unit string: "kW" and
 * "MW" scale up, anything else (or NULL) is taken as already watts.
 * Case-insensitive. */
float helios_power_to_watts(float value, const char *unit);

/* Effective cloud cover for irradiance, Helios's own weighting:
 * low + 0.6*mid + 0.2*high, each input clamped 0..100, result capped at 100. */
float helios_effective_cloud(float low_pct, float mid_pct, float high_pct);

/* Formats a power value as "<n> W", or "<n> kW" (value/1000) when use_kw,
 * with decimals (clamped 0..3) decimal places. */
void helios_format_power(float watts, bool use_kw, int decimals, char *out, size_t out_len);
