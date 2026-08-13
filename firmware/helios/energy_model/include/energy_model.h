#pragma once

/* Owns the render loop for the energy rings: starts helios/ha_ws (the
 * actual Home Assistant connection - auth, Energy Dashboard discovery,
 * live power via websocket), then periodically turns its live status plus
 * the configured ring limits (energy_config.h) into ring
 * percentages/colors and center text for ui/home. This component doesn't
 * talk to Home Assistant itself - see helios/ha_ws for that.
 * networking/settings_server's /display page reads/writes helios/ha_ws and
 * energy_config.h directly for the discovery/status UI and the limits
 * form; it doesn't go through this component either. */

/* Starts helios/ha_ws and the periodic render loop. Call once from
 * app_main(), after Wi-Fi station mode is up. */
void energy_model_start(void);

/* Recomputes and re-renders the rings immediately from the latest
 * helios/ha_ws status and energy_config.h limits, instead of waiting for
 * the next render tick - call right after saving new ring limits on
 * /display so the change is visible without delay. */
void energy_model_refresh(void);
