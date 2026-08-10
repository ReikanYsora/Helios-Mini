#pragma once

#include <stdbool.h>

/* Starts Wi-Fi in station mode using credentials stored in NVS
 * ("helios_mini" namespace, keys "wifi_ssid"/"wifi_pass"). Falls back to
 * CONFIG_HELIOS_WIFI_DEV_SSID/PASSWORD (menuconfig) when none are stored
 * yet - full SoftAP provisioning (spec Section 16) is V0.2 scope, not
 * V0.1; this only needs to prove the radio works. Non-blocking:
 * connection happens in the background with automatic retry. */
void wifi_sta_start(void);

/* Blocks until the first successful connection or timeout_ms elapses.
 * Pass 0 to poll the current state without blocking. Returns true if
 * connected. */
bool wifi_sta_wait_connected(int timeout_ms);
