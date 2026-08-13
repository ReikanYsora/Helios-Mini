#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Home Assistant WebSocket client. Zero manual entity entry: connects to
 * `<url>/api/websocket`, authenticates with the stored Long-Lived Access
 * Token, then reads Home Assistant's own Energy Dashboard configuration
 * (Settings -> Dashboards -> Energy in HA - the "energy/get_prefs"
 * command) to find each ring's live power (W) entity - the same
 * "stat_rate" / "power_config.stat_rate_from"/"stat_rate_to" entities the
 * parent Helios HA card's own power chips read - and subscribes to live
 * state-change triggers for every one of them, updating the instant Home
 * Assistant pushes one. Power only, deliberately: the cumulative energy
 * (kWh) statistics that always exist alongside these in the Energy
 * Dashboard (for HA's own billing/history graphs) are never touched, not
 * even as a fallback - a ring with no power entity linked in HA is simply
 * reported not configured. A ring can still be fed by more than one power
 * entity (uncommon, but the schema allows it) - every contributor found
 * is subscribed and summed. A source with no update in a while is
 * reported HA_WS_SOURCE_STALE rather than silently going wrong - see
 * docs/HARDWARE_REFERENCE.md for the full story, including two real bugs
 * this went through before landing here.
 *
 * helios/energy_model turns this into ring percentages/colors; this
 * component only knows about Home Assistant, never about LVGL. */

typedef enum {
    HA_WS_LINK_DISCONNECTED, /* not connected (yet, or lost - auto-retries) */
    HA_WS_LINK_CONNECTING,
    HA_WS_LINK_AUTH_FAILED,  /* connected, but the stored token was rejected */
    HA_WS_LINK_CONNECTED,    /* authenticated */
} ha_ws_link_state_t;

typedef enum {
    HA_WS_SOURCE_NOT_CONFIGURED, /* no power entity linked for this ring in HA's Energy Dashboard */
    HA_WS_SOURCE_WAITING,        /* power entity found, subscribed, no reading yet */
    HA_WS_SOURCE_LIVE,           /* has a recent power reading */
    HA_WS_SOURCE_STALE,          /* has a reading, but it's old - see HA_WS_STALE_AFTER_S */
} ha_ws_source_status_t;

#define HA_WS_ENTITY_ID_LEN 128
/* Display buffer for a ring's (possibly several, comma-joined) power
 * entity ids - wider than HA_WS_ENTITY_ID_LEN since more than one
 * contributor can be concatenated. */
#define HA_WS_ENTITY_LIST_LEN 400

typedef struct {
    ha_ws_source_status_t status;
    char entity_id[HA_WS_ENTITY_LIST_LEN]; /* underlying HA power (W) entities, comma-joined; "" if none */
    float power_w;                         /* live power, summed across contributors - LIVE/STALE only */
    int64_t age_ms;                        /* freshest contributor's age - meaningful for LIVE/STALE only */
} ha_ws_source_t;

typedef struct {
    ha_ws_link_state_t link;
    bool prefs_loaded;                 /* energy/get_prefs has answered at least once */
    bool energy_dashboard_configured;  /* prefs_loaded && at least one source found */
    ha_ws_source_t solar;
    ha_ws_source_t grid_import;
    ha_ws_source_t grid_export;
    ha_ws_source_t battery_charge;
    ha_ws_source_t battery_discharge;
} ha_ws_status_t;

/* Starts the background task owning the whole connection lifecycle:
 * connect, authenticate, discover, subscribe, reconnect on drop (Wi-Fi
 * hiccups, HA restarts, ...). Safe to call before Wi-Fi/Home Assistant
 * settings exist yet - just reports HA_WS_LINK_DISCONNECTED until the
 * settings server saves them. Call once from app_main(). */
void ha_ws_start(void);

/* Forces a fresh energy/get_prefs read and resubscribe on the current (or
 * next) connection - use after the user reconfigures the Energy Dashboard
 * in Home Assistant itself, or just wants to retry. */
void ha_ws_rescan(void);

/* Snapshot of the current link/discovery/live-value state, for the
 * /display status panel and for helios/energy_model to render rings
 * from. Safe to call from any task. */
void ha_ws_get_status(ha_ws_status_t *out);
