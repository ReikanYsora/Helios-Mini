#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Starts a small HTTP server on the station interface (the device's normal
 * LAN IP - see ui/animations/network_status.h for how that IP gets shown
 * on screen) where the user configures Home Assistant access: a base URL
 * and a Long-Lived Access Token, saved to NVS.
 *
 * This replaces the discovery/pairing custom-integration flow originally
 * sketched in spec Sections 17-18: instead of Helios Mini being discovered
 * by Home Assistant, the user points Helios Mini at Home Assistant
 * directly, the same way most self-hosted integrations bootstrap. Actually
 * *using* the saved token (a WebSocket client + energy data model) is not
 * built yet - this only covers capturing and storing the settings.
 *
 * Call once Wi-Fi station mode is up (wifi_sta_start()); safe to call
 * before the connection actually completes, since starting the server
 * doesn't require an IP yet. */
void settings_server_start(void);

/* Reads the stored Home Assistant URL/token, if both are set. Returns
 * false (and leaves the buffers empty) otherwise. */
bool settings_get_ha_config(char *url_out, size_t url_len, char *token_out, size_t token_len);
