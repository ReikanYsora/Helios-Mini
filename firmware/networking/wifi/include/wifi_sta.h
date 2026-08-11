#pragma once

#include <stdbool.h>

/* Starts Wi-Fi in station mode using credentials stored in NVS
 * ("helios_mini" namespace, keys "wifi_ssid"/"wifi_pass" - the same ones
 * networking/provisioning writes). Falls back to
 * CONFIG_HELIOS_WIFI_DEV_SSID/PASSWORD (menuconfig) only if called with no
 * stored credentials at all, which shouldn't happen in the normal flow
 * (main.c only calls this once provisioning_has_credentials() is true).
 * Non-blocking: connection happens in the background with automatic retry. */
void wifi_sta_start(void);

/* Blocks until the first successful connection or timeout_ms elapses.
 * Pass 0 to poll the current state without blocking. Returns true if
 * connected. */
bool wifi_sta_wait_connected(int timeout_ms);

typedef void (*wifi_sta_connected_cb_t)(const char *ip);

/* Registers a callback fired (with the dotted-decimal IP string) every time
 * the device gets an IP lease - including reconnects, so it stays accurate
 * across drops. Call before wifi_sta_start(). */
void wifi_sta_set_connected_cb(wifi_sta_connected_cb_t cb);
