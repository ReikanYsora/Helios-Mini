#include "sun_math.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG (M_PI / 180.0)

static double clampd(double v, double lo, double hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

sun_position_t sun_math_position(time_t utc_time, double lat, double lon)
{
    struct tm tm_utc;
    gmtime_r(&utc_time, &tm_utc);

    double H = tm_utc.tm_hour + tm_utc.tm_min / 60.0 + tm_utc.tm_sec / 3600.0;
    /* tm_yday is 0-indexed (Jan 1 = 0); Helios's doy (Date.UTC(y,0,0)-anchored)
     * is 1-indexed the same way Date.UTC(year,0,0) normalizes to Dec 31 of the
     * previous year - the two are equivalent for any instant within the day. */
    double doy = tm_utc.tm_yday + 1;

    double decl = 23.45 * sin(DEG * (360.0 / 365.0) * (doy - 81));
    double B = DEG * (360.0 / 365.0) * (doy - 81);
    double eot = 9.87 * sin(2 * B) - 7.53 * cos(B) - 1.5 * sin(B);

    /* Normalise hour angle to [-180, 180] so sign(ha) reliably gives AM/PM -
     * without it, longitudes far from Greenwich push ha out of range and flip
     * the azimuth by up to 180 degrees. */
    double ha = 15.0 * (H + lon / 15.0 + eot / 60.0 - 12.0);
    ha = fmod(fmod(ha + 180.0, 360.0) + 360.0, 360.0) - 180.0;

    double sinA = sin(DEG * lat) * sin(DEG * decl) + cos(DEG * lat) * cos(DEG * decl) * cos(DEG * ha);
    sinA = clampd(sinA, -1.0, 1.0);
    double alt = asin(sinA) / DEG;
    double cAlt = cos(alt * DEG);
    double cAz = (cAlt > 1e-4) ? (sin(DEG * decl) - sin(DEG * lat) * sinA) / (cos(DEG * lat) * cAlt) : 0.0;
    cAz = clampd(cAz, -1.0, 1.0);
    double az = acos(cAz) / DEG;
    if (ha > 0) {
        az = 360.0 - az;
    }

    sun_position_t result = { .altitude_deg = alt, .azimuth_deg = az };
    return result;
}

double sun_math_irradiance_wm2(time_t utc_time, double lat, double lon, double cloud_cover_pct)
{
    sun_position_t sun = sun_math_position(utc_time, lat, lon);
    if (sun.altitude_deg <= 0.0) {
        return 0.0; /* night - no atmosphere-path math below the horizon */
    }

    double cosZ = sin(sun.altitude_deg * DEG);
    double ghi_clear = 1098.0 * cosZ * exp(-0.059 / cosZ);

    double cc = clampd(cloud_cover_pct, 0.0, 100.0) / 100.0;
    double k_cloud = 1.0 - 0.75 * pow(cc, 3.4);

    double result = ghi_clear * k_cloud;
    return (result > 0.0) ? result : 0.0;
}
