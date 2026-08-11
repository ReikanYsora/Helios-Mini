#pragma once

/* Replaces whatever is on screen with a permanent "reach me at this
 * address" screen: the device name and its LAN URL (the same server
 * networking/settings_server exposes). Meant to be wired as
 * wifi_sta.h's connected callback, so it updates automatically on
 * (re)connect. */
void network_status_show_connected(const char *ip);
