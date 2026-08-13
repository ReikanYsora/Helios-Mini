#include "energy_config.h"
#include "storage.h"
#include "helios_config.h"
#include "energy_math.h"

#include <stdio.h>
#include <stdlib.h>

static const char *NVS_NAMESPACE = HELIOS_NVS_NAMESPACE;

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

void energy_config_load_format(energy_format_t *out)
{
    char buf[8] = {0};

    out->use_kw = ENERGY_DEFAULT_USE_KW;
    if (storage_get_string(NVS_NAMESPACE, "fmt_kw", buf, sizeof(buf)) == ESP_OK && buf[0] != '\0') {
        out->use_kw = (buf[0] == '1');
    }

    out->decimals = ENERGY_DEFAULT_DECIMALS;
    buf[0] = '\0';
    if (storage_get_string(NVS_NAMESPACE, "fmt_decimals", buf, sizeof(buf)) == ESP_OK && buf[0] != '\0') {
        int parsed = atoi(buf);
        if (parsed >= 0 && parsed <= 3) {
            out->decimals = parsed;
        }
    }
}

void energy_config_save_format(const energy_format_t *format)
{
    storage_set_string(NVS_NAMESPACE, "fmt_kw", format->use_kw ? "1" : "0");

    char buf[4];
    int decimals = format->decimals;
    if (decimals < 0) {
        decimals = 0;
    } else if (decimals > 3) {
        decimals = 3;
    }
    snprintf(buf, sizeof(buf), "%d", decimals);
    storage_set_string(NVS_NAMESPACE, "fmt_decimals", buf);
}

static bool load_page_flag(const char *key)
{
    char buf[4] = {0};
    if (storage_get_string(NVS_NAMESPACE, key, buf, sizeof(buf)) != ESP_OK || buf[0] == '\0') {
        return true; /* default: every page shown */
    }
    return buf[0] == '1';
}

static void save_page_flag(const char *key, bool value)
{
    storage_set_string(NVS_NAMESPACE, key, value ? "1" : "0");
}

void energy_config_load_pages(energy_page_visibility_t *out)
{
    out->show_solar = load_page_flag("page_solar");
    out->show_irradiance = load_page_flag("page_irrad");
    out->show_grid = load_page_flag("page_grid");
    out->show_battery = load_page_flag("page_battery");
    out->show_consumption = load_page_flag("page_consump");
}

void energy_config_save_pages(const energy_page_visibility_t *pages)
{
    save_page_flag("page_solar", pages->show_solar);
    save_page_flag("page_irrad", pages->show_irradiance);
    save_page_flag("page_grid", pages->show_grid);
    save_page_flag("page_battery", pages->show_battery);
    save_page_flag("page_consump", pages->show_consumption);
}

void energy_format_power(float watts, const energy_format_t *format, char *out, size_t out_len)
{
    energy_format_t local;
    if (format == NULL) {
        energy_config_load_format(&local);
        format = &local;
    }
    helios_format_power(watts, format->use_kw, format->decimals, out, out_len);
}
