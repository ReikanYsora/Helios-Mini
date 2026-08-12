#pragma once

/* The full extent of the Home Assistant client for now: a REST call to
 * confirm a URL + Long-Lived Access Token actually work. No WebSocket
 * client, no energy-data model yet - see docs/HARDWARE_REFERENCE.md and
 * firmware/README.md for what's still missing.
 *
 * On "token expiration": Home Assistant's Long-Lived Access Tokens don't
 * carry an expiry date the API exposes - they're valid until the user
 * revokes them in Home Assistant. So there's nothing to proactively track;
 * what we *can* do is detect a rejected token (401/403) whenever it's
 * actually used and tell the user to generate a fresh one. That's what
 * this component is for. */

typedef enum {
    HA_CLIENT_STATUS_UNKNOWN,       /* not tested yet, or url/token empty */
    HA_CLIENT_STATUS_OK,
    HA_CLIENT_STATUS_UNAUTHORIZED,  /* HA reachable, token rejected - generate a new one */
    HA_CLIENT_STATUS_UNREACHABLE,   /* couldn't connect at all - bad URL, HA down, wrong network */
} ha_client_status_t;

/* Blocking GET to `<url>/api/` with `token` as a Bearer credential.
 * Bounded by an internal timeout (a few seconds) - safe to call from an
 * httpd handler task. */
ha_client_status_t ha_client_test_connection(const char *url, const char *token);

/* Short, human-readable status text for display in the settings UI. */
const char *ha_client_status_text(ha_client_status_t status);

/* Machine-readable status key, for storing in NVS
 * ("ok" / "unauthorized" / "unreachable" / "unknown"). */
const char *ha_client_status_key(ha_client_status_t status);

/* Parses a status key back (e.g. read from NVS) into the enum. Unknown
 * strings map to HA_CLIENT_STATUS_UNKNOWN. */
ha_client_status_t ha_client_status_from_key(const char *key);
