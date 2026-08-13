/* Host unit tests for Helios Mini's pure math. Compiled by run.sh against
 * the firmware's own energy_math.c and sun_math.c - no ESP-IDF, no
 * hardware, so the Helios-parity formulas stay pinned across refactors. */

#include "energy_math.h"
#include "sun_math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_checks;
static int g_failures;

static void check(int cond, const char *expr, const char *file, int line)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("  FAIL %s:%d  %s\n", file, line, expr);
    }
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)
#define APPROX(a, b) check(fabs((double)(a) - (double)(b)) <= 0.01, #a " ~= " #b, __FILE__, __LINE__)

static void test_clamp_percent(void)
{
    APPROX(helios_clamp_percent(500.0f, 1000.0f), 50.0f);
    APPROX(helios_clamp_percent(2000.0f, 1000.0f), 100.0f);
    APPROX(helios_clamp_percent(-5.0f, 1000.0f), 0.0f);
    APPROX(helios_clamp_percent(100.0f, 0.0f), 0.0f);
    APPROX(helios_clamp_percent(100.0f, -5.0f), 0.0f);
}

static void test_consumption_load(void)
{
    APPROX(helios_consumption_load(2000.0f, 500.0f, 0.0f, 0.0f, 0.0f), 2500.0f);
    APPROX(helios_consumption_load(2000.0f, 0.0f, 0.0f, 1000.0f, 0.0f), 1000.0f);
    APPROX(helios_consumption_load(0.0f, 0.0f, 0.0f, 0.0f, 1000.0f), 1000.0f);
    APPROX(helios_consumption_load(0.0f, 0.0f, 5000.0f, 0.0f, 0.0f), 0.0f);
    APPROX(helios_consumption_load(1000.0f, 2000.0f, 500.0f, 300.0f, 800.0f), 3000.0f);
}

static void test_power_to_watts(void)
{
    APPROX(helios_power_to_watts(5.0f, "kW"), 5000.0f);
    APPROX(helios_power_to_watts(2.0f, "MW"), 2000000.0f);
    APPROX(helios_power_to_watts(100.0f, "W"), 100.0f);
    APPROX(helios_power_to_watts(100.0f, NULL), 100.0f);
    APPROX(helios_power_to_watts(5.0f, "kw"), 5000.0f);
}

static void test_effective_cloud(void)
{
    APPROX(helios_effective_cloud(100.0f, 0.0f, 0.0f), 100.0f);
    APPROX(helios_effective_cloud(0.0f, 100.0f, 0.0f), 60.0f);
    APPROX(helios_effective_cloud(0.0f, 0.0f, 100.0f), 20.0f);
    APPROX(helios_effective_cloud(50.0f, 50.0f, 50.0f), 90.0f);
    APPROX(helios_effective_cloud(100.0f, 100.0f, 100.0f), 100.0f);
    APPROX(helios_effective_cloud(-10.0f, 200.0f, 50.0f), 70.0f);
}

static void test_format_power(void)
{
    char buf[24];
    helios_format_power(1234.0f, false, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "1234 W") == 0);
    helios_format_power(1234.0f, true, 2, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.23 kW") == 0);
    helios_format_power(1234.0f, false, 1, buf, sizeof(buf));
    CHECK(strcmp(buf, "1234.0 W") == 0);
    helios_format_power(500.0f, true, 3, buf, sizeof(buf));
    CHECK(strcmp(buf, "0.500 kW") == 0);
    helios_format_power(1500.0f, false, 5, buf, sizeof(buf)); /* decimals clamped to 3 */
    CHECK(strcmp(buf, "1500.000 W") == 0);
}

static time_t utc(int year, int mon, int mday, int hour)
{
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    return timegm(&t);
}

static void test_sun_math(void)
{
    /* Solstice local noon at the equator (lon 0 -> 12:00 UTC): sun high,
     * clear-sky GHI in a plausible band. */
    time_t noon = utc(2024, 6, 21, 12);
    double clear = sun_math_irradiance_wm2(noon, 0.0, 0.0, 0.0);
    CHECK(clear > 500.0 && clear < 1100.0);

    /* Full cloud attenuates but never goes negative. */
    double cloudy = sun_math_irradiance_wm2(noon, 0.0, 0.0, 100.0);
    CHECK(cloudy < clear && cloudy >= 0.0);

    /* Local midnight -> sun below horizon -> exactly 0. */
    time_t midnight = utc(2024, 6, 21, 0);
    CHECK(sun_math_irradiance_wm2(midnight, 0.0, 0.0, 0.0) == 0.0);

    CHECK(sun_math_position(noon, 0.0, 0.0).altitude_deg > 0.0);
    CHECK(sun_math_position(midnight, 0.0, 0.0).altitude_deg < 0.0);
}

int main(void)
{
    test_clamp_percent();
    test_consumption_load();
    test_power_to_watts();
    test_effective_cloud();
    test_format_power();
    test_sun_math();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
