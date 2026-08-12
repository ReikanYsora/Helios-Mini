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

/* Every way fetching a single entity's numeric state can turn out - this is
 * what backs the "gere TOUS LES CAS" entity status panel on /display. */
typedef enum {
    HA_ENTITY_STATUS_OK,
    HA_ENTITY_STATUS_NOT_CONFIGURED,  /* entity_id left empty - feature intentionally unused */
    HA_ENTITY_STATUS_NOT_FOUND,       /* HA reachable, no such entity_id (404) */
    HA_ENTITY_STATUS_UNAVAILABLE,     /* entity exists but its state is unavailable/unknown */
    HA_ENTITY_STATUS_NOT_NUMERIC,     /* state exists but isn't a plain number - wrong entity picked */
    HA_ENTITY_STATUS_UNAUTHORIZED,    /* token rejected */
    HA_ENTITY_STATUS_UNREACHABLE,     /* couldn't connect at all, or HA/URL not configured */
} ha_entity_status_t;

/* Blocking GET to `<url>/api/states/<entity_id>`, parses the JSON `state`
 * field as a float into *value_out. Understands `kW`/`MW`
 * `unit_of_measurement` attributes and normalizes to watts; anything else
 * (including plain `W` or no unit at all) is taken as already being watts.
 * Bounded by an internal timeout - safe to call from a background task or
 * an httpd handler. *value_out is only meaningful when the return is
 * HA_ENTITY_STATUS_OK. */
ha_entity_status_t ha_client_get_entity_power(const char *url, const char *token,
                                               const char *entity_id, float *value_out);

/* Short, human-readable status text for the entity status panel. */
const char *ha_entity_status_text(ha_entity_status_t status);
