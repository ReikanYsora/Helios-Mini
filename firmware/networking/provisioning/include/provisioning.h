#pragma once

#include <stdbool.h>

/* True if Wi-Fi credentials are already stored in NVS
 * ("helios_mini" namespace, "wifi_ssid" key - same one wifi_sta.c reads). */
bool provisioning_has_credentials(void);

/* True if the BOOT button is held down right now - an escape hatch to force
 * re-entering Wi-Fi setup (e.g. after a password change) without erasing
 * anything else. Call this as early as possible in app_main(), before any
 * slow init steps eat into the window the user is holding the button. */
bool provisioning_boot_forced(void);

/* Starts an open SoftAP named "HELIOS-MINI-XXXX" (XXXX from the station
 * MAC) plus a small HTTP server at http://192.168.4.1/ where the user picks
 * a Wi-Fi network (scanned live, or typed manually) and enters its
 * password. No app, no captive-portal auto-popup, no JavaScript - just a
 * plain form, matching spec Section 16's "no dev tools" requirement.
 *
 * Non-blocking: the AP and HTTP server run in their own FreeRTOS tasks and
 * this returns immediately. On submit, credentials are saved to NVS and the
 * device reboots; on the next boot, provisioning_has_credentials() is true
 * and main.c calls wifi_sta_start() instead.
 *
 * display_init() must have been called first - this shows a short "connect
 * to HELIOS-MINI-XXXX" notice on screen. */
void provisioning_start_portal(void);
