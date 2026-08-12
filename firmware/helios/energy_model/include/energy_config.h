#pragma once

#include <stdbool.h>
#include <stddef.h>

/* NVS-backed configuration for the energy rings: which Home Assistant
 * entity feeds each ring, and the max-power reference each ring's 100%
 * represents. Deliberately lives in helios/energy_model, not
 * networking/settings_server - energy_model.c (the poller) reads it
 * directly with no dependency on the settings app; settings_server.c also
 * links against this component to build the /display page's forms. Battery
 * is modeled as two separate entities (charge/discharge), matching how the
 * parent Helios HA card does it - not one signed value. */

#define ENERGY_ENTITY_ID_LEN 128

typedef struct {
    char solar[ENERGY_ENTITY_ID_LEN];             /* PV production power, W */
    char grid_import[ENERGY_ENTITY_ID_LEN];       /* power drawn from the grid, W */
    char grid_export[ENERGY_ENTITY_ID_LEN];       /* power sent to the grid, W */
    char battery_charge[ENERGY_ENTITY_ID_LEN];    /* battery charge power, W */
    char battery_discharge[ENERGY_ENTITY_ID_LEN]; /* battery discharge power, W */
    char home[ENERGY_ENTITY_ID_LEN];              /* home consumption, W - center text */
} energy_entity_config_t;

typedef struct {
    float max_solar_w;
    float max_grid_import_w;
    float max_grid_export_w;
    float max_battery_w;
} energy_limits_t;

/* Defaults for a fresh device - nothing configured yet still renders
 * something sane the moment entities are filled in. max_solar_w mirrors
 * Helios's own DEFAULT_MAX_EXPECTED_POWER_W. */
#define ENERGY_DEFAULT_MAX_SOLAR_W       5000.0f
#define ENERGY_DEFAULT_MAX_GRID_IMPORT_W 5000.0f
#define ENERGY_DEFAULT_MAX_GRID_EXPORT_W 5000.0f
#define ENERGY_DEFAULT_MAX_BATTERY_W     3000.0f

/* Reads whatever's stored, filling each field the caller didn't have with
 * an empty string ("" = that ring's entity isn't configured). */
void energy_config_load_entities(energy_entity_config_t *out);
void energy_config_save_entities(const energy_entity_config_t *config);

/* Reads stored limits, falling back to the ENERGY_DEFAULT_* constants above
 * for anything never saved (or saved as an invalid/non-positive number). */
void energy_config_load_limits(energy_limits_t *out);
void energy_config_save_limits(const energy_limits_t *limits);
