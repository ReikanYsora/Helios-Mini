#pragma once

#include <stdbool.h>
#include "ha_client.h"

/* Per-ring live status for the /display status panel - "gere TOUS LES CAS":
 * whether an entity is configured at all, and if so what the last poll of
 * it actually returned. */
typedef struct {
    bool configured;           /* an entity id is saved for this ring at all */
    ha_entity_status_t status; /* last poll result - only meaningful if configured */
    float last_value_w;        /* only meaningful when status == HA_ENTITY_STATUS_OK */
} energy_entity_status_t;

typedef struct {
    energy_entity_status_t solar;
    energy_entity_status_t grid_import;
    energy_entity_status_t grid_export;
    energy_entity_status_t battery_charge;
    energy_entity_status_t battery_discharge;
    energy_entity_status_t home;
    bool ha_configured;   /* Home Assistant URL + token are both saved */
    bool polled_once;     /* at least one poll cycle has completed */
} energy_model_status_t;

/* Starts the background polling task (10s interval): reads the configured
 * Home Assistant URL/token (networking/settings_server's NVS keys, read
 * directly - this component intentionally doesn't depend on
 * settings_server) and entity ids (energy_config.h), fetches each
 * configured entity's power via helios/ha_client, computes the three ring
 * percentages/colors and the center consumption text, and pushes them to
 * ui/home's energy_rings_update(). Switches the screen from the persistent
 * IP notice to the rings the first time the "home" entity is configured.
 * Safe to call before Wi-Fi/Home Assistant are set up - it just reports
 * "not configured" until they are. Call once from app_main(), after
 * settings_server_start(). */
void energy_model_start(void);

/* Re-reads entity/limit configuration from NVS and polls immediately,
 * instead of waiting for the next 10s tick - call right after saving new
 * /display settings so the screen and status panel update without delay. */
void energy_model_refresh_config(void);

/* Snapshot of the last poll's per-entity status, for the /display status
 * panel. Safe to call from the httpd task. */
void energy_model_get_status(energy_model_status_t *out);
