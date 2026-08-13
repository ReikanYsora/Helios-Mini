#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

/* MQTT bridge to Home Assistant: exposes a handful of on-device controls
 * and diagnostics as MQTT entities, auto-discovered by HA
 * (homeassistant/<component>/.../config, retained) the moment this
 * connects - no manual entity setup in HA beyond enabling MQTT discovery,
 * which is on by default.
 *
 * Entities: screen brightness (number, writable), Wi-Fi signal + connected
 * (sensor/binary_sensor), free heap/PSRAM + uptime (diagnostic sensors),
 * and the same ground irradiance (W/m²) + effective cloud cover (%)
 * helios/irradiance_model feeds the irradiance ring/page. */

typedef struct {
    char host[128];
    int port;      /* 0 = use the default (1883) */
    char username[64];
    char password[64];
    char device_name[64]; /* empty = MQTT_DEFAULT_DEVICE_NAME. Shown as the HA device name every discovered entity groups under - see mqtt_bridge_build_device_json(). */
    bool enabled;
} mqtt_config_t;

#define MQTT_DEFAULT_DEVICE_NAME "Helios Mini"

#define MQTT_DEFAULT_PORT 1883

/* Reads stored config, falling back to MQTT_DEFAULT_PORT for an unset/
 * invalid port and enabled=false for a fresh device. */
void mqtt_config_load(mqtt_config_t *out);
void mqtt_config_save(const mqtt_config_t *cfg);

typedef enum {
    MQTT_BRIDGE_DISABLED,    /* not enabled, or host empty - not configured */
    MQTT_BRIDGE_CONNECTING,
    MQTT_BRIDGE_CONNECTED,
    MQTT_BRIDGE_ERROR,       /* connection attempt failed/dropped, retrying */
} mqtt_bridge_state_t;

/* Starts (or no-ops if disabled/unconfigured) the MQTT connection and its
 * background publish loop. Call once from app_main(), after Wi-Fi station
 * mode is up. Safe to call even with no config yet - stays
 * MQTT_BRIDGE_DISABLED until mqtt_bridge_restart() is called after a
 * config save. */
void mqtt_bridge_start(void);

/* Requests a teardown + reconnect with whatever config is stored now -
 * call after saving new settings on /mqtt so a changed broker/credentials
 * take effect immediately instead of waiting for a reboot. Returns
 * immediately: the actual esp_mqtt_client_init()/start() work happens on
 * the background publish task, not the caller's own stack/task - matters
 * because the caller is normally the settings httpd handler, and a slow
 * or stuck broker connect must never block it from sending its HTTP
 * response (see docs/HARDWARE_REFERENCE.md - a real "device unreachable"
 * incident this session traced back to exactly that). */
void mqtt_bridge_restart(void);

mqtt_bridge_state_t mqtt_bridge_get_state(void);

/* Short, human-readable text for the /mqtt status pill. */
const char *mqtt_bridge_state_text(mqtt_bridge_state_t state);

/* base_topic_out is filled with "helios_mini/<id>" (no trailing slash);
 * returns false (leaves it untouched) if the bridge isn't connected. */
bool mqtt_bridge_get_base_topic(char *out, size_t out_len);
/* Publishes retained if retain is true. No-op (returns false) if not
 * currently connected - callers should just skip the publish, there's
 * nothing queued/retried here. */
bool mqtt_bridge_publish(const char *topic_suffix, const char *payload, bool retain);

/* Same as mqtt_bridge_publish() but topic is the FULL topic string, not
 * relative to the base topic - for HA discovery configs, which live under
 * "homeassistant/<component>/..." rather than "helios_mini/<id>/...".
 * Always retained (discovery configs are meant to persist on the broker
 * so HA sees the entity again even if it was offline at publish time). */
bool mqtt_bridge_publish_raw_config(const char *topic, const char *payload);

/* Registers a handler for "<base topic>/<topic_suffix>" - (re)subscribed
 * automatically on every (re)connect. Fixed small table, not meant for a
 * large/dynamic entity set. Call before mqtt_bridge_start() so the very
 * first connect already subscribes it. */
typedef void (*mqtt_bridge_command_cb_t)(const char *payload, size_t payload_len);
void mqtt_bridge_set_command_handler(const char *topic_suffix, mqtt_bridge_command_cb_t cb);

/* Registers a callback fired once per (re)connect, right after this
 * component has published its own discovery configs and subscribed its
 * command handlers. Single slot; call before mqtt_bridge_start(). */
typedef void (*mqtt_bridge_connected_cb_t)(void);
void mqtt_bridge_set_connected_cb(mqtt_bridge_connected_cb_t cb);

/* Builds the shared HA MQTT discovery "device" object (identifiers/name/
 * manufacturer/model/sw_version) every entity groups under - name comes
 * from mqtt_config_t.device_name (or MQTT_DEFAULT_DEVICE_NAME if unset).
 * Caller owns the returned cJSON (add it into a discovery payload with
 * cJSON_AddItemToObject(), which takes ownership, or cJSON_Delete() it
 * directly). */
cJSON *mqtt_bridge_build_device_json(void);
