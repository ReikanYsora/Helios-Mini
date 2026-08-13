#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Starts a small HTTP server on the station interface (the device's normal
 * LAN IP - see ui/animations/network_status.h for how that IP gets shown
 * on screen). A topbar (Helios logo + live Wi-Fi/Home Assistant status
 * icons) and a left sidebar (Network / Home Assistant / Display / MQTT / Debug)
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
 *   /display, /display/save, Live status of the three energy rings' Home
 *   /display/rescan         Assistant sources (helios/ha_ws) - which
 *                            entity feeds each ring is auto-discovered
 *                            from Home Assistant's own Energy Dashboard
 *                            configuration, nothing typed here - plus the
 *                            rings' 100% power references (/display/save)
 *                            and a manual re-discovery trigger
 *                            (/display/rescan). Status panel is never
 *                            silently blank: not set up in the Energy
 *                            Dashboard / Home Assistant not set up at all /
 *                            waiting for a first live update / a live
 *                            value (flagged stale if it stops updating).
 *   /mqtt, /mqtt/save       Optional MQTT bridge: exposes on-device
 *                            controls/diagnostics to Home Assistant as
 *                            auto-discovered MQTT entities (helios/mqtt_bridge).
 *   /debug and its sub-paths live system status (uptime/heap/PSRAM/Wi-Fi
 *                            RSSI), a screenshot download, and a factory
 *                            reset (diagnostics).
 *
 * This replaces the discovery/pairing custom-integration flow originally
 * sketched in spec Sections 17-18: instead of Helios Mini being discovered
 * by Home Assistant, the user points Helios Mini at Home Assistant
 * directly, the same way most self-hosted integrations bootstrap.
 * helios/ha_ws then does the rest over a websocket - auth, Energy
 * Dashboard discovery, live updates - with no further manual setup.
 *
 * Call once Wi-Fi station mode is up (wifi_sta_start()); safe to call
 * before the connection actually completes, since starting the server
 * doesn't require an IP yet. */
void settings_server_start(void);
