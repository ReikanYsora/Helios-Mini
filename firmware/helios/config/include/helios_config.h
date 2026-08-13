#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Every Helios Mini setting lives in one NVS namespace; storage_erase_all()
 * on it is the whole factory-reset story. */
#define HELIOS_NVS_NAMESPACE "helios_mini"

/* NVS keys shared by more than one component. Component-private keys
 * (energy limits, MQTT config) stay with their own component. */
#define HELIOS_NVS_KEY_HA_URL    "ha_url"
#define HELIOS_NVS_KEY_HA_TOKEN  "ha_token"
#define HELIOS_NVS_KEY_HA_STATUS "ha_status"
#define HELIOS_NVS_KEY_WIFI_SSID "wifi_ssid"
#define HELIOS_NVS_KEY_WIFI_PASS "wifi_pass"

/* Source colors, matching the Helios card's chip-appearance.ts. */
#define HELIOS_COLOR_SOLAR             0xFF9800
#define HELIOS_COLOR_IRRADIANCE        0xFFC107
#define HELIOS_COLOR_GRID_IMPORT       0x488FC2
#define HELIOS_COLOR_GRID_EXPORT       0x8353D1
#define HELIOS_COLOR_BATTERY_CHARGE    0xF06292
#define HELIOS_COLOR_BATTERY_DISCHARGE 0x4DB6AC

/* Reads the stored Home Assistant URL + token. Returns false, with both
 * buffers emptied, unless both are set. */
bool helios_config_get_ha_credentials(char *url, size_t url_len,
                                      char *token, size_t token_len);
