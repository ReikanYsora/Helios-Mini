#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Starts a low-priority background task that periodically logs free heap,
 * PSRAM, and uptime. Precursor to the full on-screen diagnostics page
 * (spec Section 24), which lands in V0.4 as a UI overlay reading these
 * same figures. */
void diagnostics_start(void);

typedef struct {
    int64_t uptime_s;
    uint32_t free_heap;
    uint32_t free_psram;
    bool wifi_connected;
    int8_t wifi_rssi; /* dBm, only meaningful if wifi_connected */
} diagnostics_status_t;

/* Point-in-time system status snapshot, for a debug/settings web page -
 * cheap enough to call on every page load. */
void diagnostics_get_status(diagnostics_status_t *out);
