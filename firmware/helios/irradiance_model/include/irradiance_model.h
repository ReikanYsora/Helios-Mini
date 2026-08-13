#pragma once

#include <stdbool.h>

/* Solar irradiance (W/m²) for a dedicated ring/page - the theoretical
 * "how much sun is actually hitting the ground here right now" figure,
 * independent of any PV system (unlike the solar *production* ring/page,
 * which reads a real inverter's power entity). Ported from Helios's own
 * built-in irradiance fallback (src/core/time/sun.ts): the home's lat/lon
 * comes straight from Home Assistant's own config (zero manual entry,
 * same philosophy as the Energy Dashboard auto-discovery), and cloud
 * cover comes from Open-Meteo's free forecast API. No entity to link, no
 * HA-side sensor required - if a device has none, this ring/page still
 * works from location + weather alone. */

typedef enum {
    IRRADIANCE_NOT_CONFIGURED, /* Home Assistant not set up on this device yet - no location to work from */
    IRRADIANCE_WAITING,        /* HA configured, still resolving the home's location and/or the first Open-Meteo fetch */
    IRRADIANCE_LIVE,
} irradiance_status_t;

typedef struct {
    irradiance_status_t status;
    float wm2;             /* current effective ground-horizontal irradiance - 0 at night, recomputed live every call */
    float cloud_cover_pct; /* last Open-Meteo effective cloud cover (0-100), for a caption - meaningless until status is LIVE */
} irradiance_reading_t;

/* Starts the background task that discovers the home's lat/lon from Home
 * Assistant's own /api/config and periodically refreshes cloud cover from
 * Open-Meteo. Both are retried forever on failure (no HA yet, no network
 * yet, Open-Meteo hiccup, ...), never a one-shot give-up. Call once from
 * app_main(), after Wi-Fi station mode is up. */
void irradiance_model_start(void);

/* Cheap and synchronous - recomputes the CURRENT irradiance from the
 * cached home location + cached cloud cover using live sun position (pure
 * trig, no I/O, no blocking). Safe to call every energy_model render
 * tick, same as ha_ws_get_status(). */
irradiance_reading_t irradiance_model_get(void);
