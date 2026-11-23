#include "nav_format.h"

#include <stdio.h>

int nav_format_meters(char *out, size_t cap, int32_t meters)
{
    if (meters < 0) {
        if (cap >= 1) out[0] = '\0';
        if (cap >= 3) {
            out[0] = '-';
            out[1] = '-';
            out[2] = '\0';
            return 2;
        }
        return 0;
    }

    if (meters < 1000) {
        return snprintf(out, cap, "%ldm", (long)meters);
    }

    const uint32_t km = (uint32_t)meters / 1000u;
    const uint32_t frac = ((uint32_t)meters % 1000u) / 10u;  // 两位小数, 截断
    return snprintf(out, cap, "%lu.%02lukm", (unsigned long)km,
                    (unsigned long)frac);
}

nav_off_level_t nav_off_level(int32_t off_m)
{
    if (off_m < 0) return NAV_OFF_OK;
    if (off_m >= NAV_OFF_ALERT_M) return NAV_OFF_ALERT;
    if (off_m >= NAV_OFF_WARN_M) return NAV_OFF_WARN;
    return NAV_OFF_OK;
}
