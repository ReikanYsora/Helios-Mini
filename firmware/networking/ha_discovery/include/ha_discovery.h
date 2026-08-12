#pragma once

#include <stddef.h>

typedef struct {
    char name[64];
    char url[128];
} ha_discovery_result_t;

/* Blocking mDNS browse for "_home-assistant._tcp.local." instances on the
 * LAN - the service Home Assistant's own Zeroconf integration advertises
 * itself under. Writes up to `max_results` entries into `out` and returns
 * how many were found. Prefers an advertised "base_url" TXT record; falls
 * back to "http://<resolved-ip>:<port>" when a result doesn't have one.
 * Takes roughly `timeout_ms` to return - safe to call from an httpd
 * handler task, but callers should show that a scan is running. */
int ha_discovery_scan(ha_discovery_result_t *out, int max_results, int timeout_ms);
