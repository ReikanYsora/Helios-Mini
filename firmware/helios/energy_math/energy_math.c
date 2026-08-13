#include "energy_math.h"

#include <stdio.h>
#include <strings.h>

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

float helios_clamp_percent(float value_w, float max_w)
{
    if (max_w <= 0.0f) {
        return 0.0f;
    }
    return clampf((value_w / max_w) * 100.0f, 0.0f, 100.0f);
}

float helios_consumption_load(float production_w, float grid_import_w, float grid_export_w,
                              float battery_charge_w, float battery_discharge_w)
{
    float net_battery = battery_charge_w - battery_discharge_w;
    float load = production_w + grid_import_w - grid_export_w - net_battery;
    return load > 0.0f ? load : 0.0f;
}

float helios_power_to_watts(float value, const char *unit)
{
    if (unit != NULL) {
        if (strcasecmp(unit, "kW") == 0) {
            return value * 1000.0f;
        }
        if (strcasecmp(unit, "MW") == 0) {
            return value * 1000000.0f;
        }
    }
    return value;
}

float helios_effective_cloud(float low_pct, float mid_pct, float high_pct)
{
    float eff = clampf(low_pct, 0.0f, 100.0f)
              + 0.6f * clampf(mid_pct, 0.0f, 100.0f)
              + 0.2f * clampf(high_pct, 0.0f, 100.0f);
    return eff > 100.0f ? 100.0f : eff;
}

void helios_format_power(float watts, bool use_kw, int decimals, char *out, size_t out_len)
{
    if (decimals < 0) {
        decimals = 0;
    } else if (decimals > 3) {
        decimals = 3;
    }
    if (use_kw) {
        snprintf(out, out_len, "%.*f kW", decimals, (double)(watts / 1000.0f));
    } else {
        snprintf(out, out_len, "%.*f W", decimals, (double)watts);
    }
}
