#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Starts a low-priority background task that samples system health every
 * few seconds - die temperature, per-core CPU load, free internal heap,
 * free PSRAM, Wi-Fi signal - logging a summary line periodically and
 * keeping a short rolling history for the /debug page's live graphs. */
void diagnostics_start(void);

typedef struct {
    int64_t uptime_s;
    uint32_t free_heap;     /* internal RAM free now */
    uint32_t min_free_heap; /* internal RAM low-water since boot */
    uint32_t free_psram;
    bool wifi_connected;
    int8_t wifi_rssi;       /* dBm, only meaningful if wifi_connected */
    float temperature_c;    /* ESP32-S3 internal (die) temperature */
    float cpu0_pct;         /* PRO core load over the last sample interval */
    float cpu1_pct;         /* APP core load over the last sample interval */
} diagnostics_status_t;

/* Point-in-time system status snapshot, for a debug/settings web page or
 * MQTT publish - cheap enough to call on every page load. */
void diagnostics_get_status(diagnostics_status_t *out);

/* Number of samples retained for the /debug graphs (one every ~5 s, so this
 * many covers roughly the last five minutes). */
#define DIAGNOSTICS_HISTORY 60

/* One point in the rolling history behind the live graphs. */
typedef struct {
    float temperature_c;
    float cpu0_pct;
    float cpu1_pct;
    uint32_t free_heap;
    uint32_t free_psram;
    int8_t wifi_rssi;
} diagnostics_sample_t;

/* Copies up to `max` most-recent samples (oldest first) into `out`,
 * returning how many were written in `*out_count`. Safe to call from
 * another task - the copy is taken under a spinlock. */
void diagnostics_get_history(diagnostics_sample_t *out, size_t max, size_t *out_count);
