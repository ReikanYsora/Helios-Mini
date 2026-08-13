#pragma once

#include <stdbool.h>
#include <stddef.h>

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
 * across drops. Only one callback slot - call before wifi_sta_start(), and
 * have it do everything that needs to happen on an IP change (main.c's own
 * wrapper both shows the IP notice and forwards to ui/home's IP screen). */
void wifi_sta_set_connected_cb(wifi_sta_connected_cb_t cb);

/* Writes the station's current dotted-decimal IP into out (queried live
 * from the netif, not cached from the last connected-callback firing), or
 * "" if not connected/no lease yet. Returns true iff an address was
 * written. Safe to call from any task at any time. */
bool wifi_sta_get_ip(char *out, size_t out_len);

/* Toggles the "HELIOS-MINI-XXXX" setup network on top of the existing
 * station connection (WIFI_MODE_APSTA vs WIFI_MODE_STA) - lets a second
 * device join the AP and reach the same settings_server (also bound to
 * the AP's 192.168.4.1 once it's up) without disturbing the station
 * connection. This is separate from networking/provisioning's own AP,
 * which only ever runs before any Wi-Fi credentials exist; this one is
 * for reopening setup access on demand while already connected. Call
 * only after wifi_sta_start(). Returns false on a Wi-Fi API failure. */
bool wifi_sta_set_setup_ap_enabled(bool enable);
bool wifi_sta_is_setup_ap_enabled(void);

/* Writes the setup AP's SSID ("HELIOS-MINI-XXXX") into `out`, regardless
 * of whether it's currently enabled. */
void wifi_sta_get_setup_ap_ssid(char *out, size_t out_size);
