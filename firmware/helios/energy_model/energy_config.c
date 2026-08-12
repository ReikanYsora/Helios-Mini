#include "energy_config.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *NVS_NAMESPACE = "helios_mini";

static void load_entity(const char *key, char *out, size_t out_len)
{
    out[0] = '\0';
    storage_get_string(NVS_NAMESPACE, key, out, out_len);
}

void energy_config_load_entities(energy_entity_config_t *out)
{
    load_entity("e_solar", out->solar, sizeof(out->solar));
    load_entity("e_gimport", out->grid_import, sizeof(out->grid_import));
    load_entity("e_gexport", out->grid_export, sizeof(out->grid_export));
    load_entity("e_bcharge", out->battery_charge, sizeof(out->battery_charge));
    load_entity("e_bdischarge", out->battery_discharge, sizeof(out->battery_discharge));
    load_entity("e_home", out->home, sizeof(out->home));
}

void energy_config_save_entities(const energy_entity_config_t *config)
{
    storage_set_string(NVS_NAMESPACE, "e_solar", config->solar);
    storage_set_string(NVS_NAMESPACE, "e_gimport", config->grid_import);
    storage_set_string(NVS_NAMESPACE, "e_gexport", config->grid_export);
    storage_set_string(NVS_NAMESPACE, "e_bcharge", config->battery_charge);
    storage_set_string(NVS_NAMESPACE, "e_bdischarge", config->battery_discharge);
    storage_set_string(NVS_NAMESPACE, "e_home", config->home);
}

static float load_limit(const char *key, float fallback)
{
    char buf[32] = {0};
    if (storage_get_string(NVS_NAMESPACE, key, buf, sizeof(buf)) != ESP_OK || buf[0] == '\0') {
        return fallback;
    }
    char *end = NULL;
    float parsed = strtof(buf, &end);
    if (end == buf || parsed <= 0.0f) {
        return fallback;
    }
    return parsed;
}

void energy_config_load_limits(energy_limits_t *out)
{
    out->max_solar_w = load_limit("lim_solar", ENERGY_DEFAULT_MAX_SOLAR_W);
    out->max_grid_import_w = load_limit("lim_gimport", ENERGY_DEFAULT_MAX_GRID_IMPORT_W);
    out->max_grid_export_w = load_limit("lim_gexport", ENERGY_DEFAULT_MAX_GRID_EXPORT_W);
    out->max_battery_w = load_limit("lim_battery", ENERGY_DEFAULT_MAX_BATTERY_W);
}

static void save_limit(const char *key, float value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", value);
    storage_set_string(NVS_NAMESPACE, key, buf);
}

void energy_config_save_limits(const energy_limits_t *limits)
{
    save_limit("lim_solar", limits->max_solar_w);
    save_limit("lim_gimport", limits->max_grid_import_w);
    save_limit("lim_gexport", limits->max_grid_export_w);
    save_limit("lim_battery", limits->max_battery_w);
}
