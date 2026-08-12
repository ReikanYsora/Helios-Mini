#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Starts a small HTTP server on the station interface (the device's normal
 * LAN IP - see ui/animations/network_status.h for how that IP gets shown
 * on screen). A topbar (Helios logo + live Wi-Fi/Home Assistant status
 * icons) and a left sidebar (Network / Home Assistant / Display / Debug)
 * frame every page - icons are inline MDI SVGs (see mdi_icons.h), the same
 * icon set Home Assistant's own frontend uses, no external requests. Routes:
 *   /                       redirects to /network.
 *   /network                Wi-Fi connection status; toggle for the
 *                            "HELIOS-MINI-XXXX" setup AP alongside the
 *                            existing station connection (wifi_sta.h).
 *   /ha, /ha/save, /ha/test Home Assistant URL + Long-Lived Access Token
 *                            form. Saving tests the connection immediately
 *                            (helios/ha_client) and shows the result.
 *   /ha/scan                mDNS search (networking/ha_discovery) for Home
 *                            Assistant on the LAN; picking a result
 *                            prefills the URL field, Save still commits it.
 *   /display, /display/save Which Home Assistant entities feed the three
 *                            energy rings (helios/energy_model) and their
 *                            100% power references, plus a live per-entity
 *                            status panel (not configured / Home Assistant
 *                            not set up / not tested yet / a real error /
 *                            OK with the value) - saving tests every filled
 *                            in entity immediately, same as /ha/save.
 *   /debug and its sub-paths hardware self-tests (screen/speaker/microphone)
 *                            plus a live system status snapshot
 *                            (diagnostics). No gyroscope test - this board
 *                            doesn't have one.
 *
 * This replaces the discovery/pairing custom-integration flow originally
 * sketched in spec Sections 17-18: instead of Helios Mini being discovered
 * by Home Assistant, the user points Helios Mini at Home Assistant
 * directly, the same way most self-hosted integrations bootstrap. Actually
 * *using* the saved token (a WebSocket client + energy data model) is not
 * built yet - this only covers capturing, storing, and validating the
 * settings.
 *
 * Call once Wi-Fi station mode is up (wifi_sta_start()); safe to call
 * before the connection actually completes, since starting the server
 * doesn't require an IP yet. */
void settings_server_start(void);

/* Reads the stored Home Assistant URL/token, if both are set. Returns
 * false (and leaves the buffers empty) otherwise. */
bool settings_get_ha_config(char *url_out, size_t url_len, char *token_out, size_t token_len);
