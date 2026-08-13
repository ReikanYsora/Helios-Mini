#pragma once

#include <time.h>

/* Solar position and irradiance math: pure functions, no I/O, no LVGL, no
 * Home Assistant. Ported line-for-line from Helios's own
 * src/core/time/sun.ts (getSunPosition / computeIrradianceWm2), validated
 * there against NOAA SPA over a year x 8 latitudes (mean altitude error
 * 0.30 deg, azimuth 0.36 deg, max altitude error ~1 deg) - plenty for a
 * W/m² estimate. Kept as its own header so it stays trivially portable
 * and testable independent of irradiance_model's networking/NVS side. */

typedef struct {
    double altitude_deg; /* degrees above the horizon, negative = below (night) */
    double azimuth_deg;  /* degrees clockwise from north */
} sun_position_t;

/* Sun altitude/azimuth at a UTC instant for a lat/lon point. */
sun_position_t sun_math_position(time_t utc_time, double lat, double lon);

/* Effective ground-horizontal irradiance in W/m² - Haurwitz clear-sky GHI
 * times a Kasten-Czeplak-style cloud attenuation curve, exactly Helios's
 * computeIrradianceWm2(). 0 below the horizon (night), never negative.
 * cloud_cover_pct is the 0-100 "effective" cloud cover - see
 * irradiance_model.c's low + 0.6*mid + 0.2*high blend, same weighting
 * Helios's own Open-Meteo fetch uses. */
double sun_math_irradiance_wm2(time_t utc_time, double lat, double lon, double cloud_cover_pct);
