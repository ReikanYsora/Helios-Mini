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

/* Hardware self-tests for a debug/factory-test UI (spec Section 23's
 * factory test list, minus what this board doesn't have - no gyroscope/IMU
 * on the Waveshare ESP32-S3-Touch-AMOLED-1.32, unlike some other Waveshare
 * AMOLED variants). Each is short, blocking, and user-visible/audible -
 * safe to call from an httpd handler task, but the result can only really
 * be judged by a person looking at/listening to the device. */
void diagnostics_test_screen(void);        /* flashes a few colors, then restores black */
void diagnostics_test_speaker(void);       /* plays the startup tone */
int16_t diagnostics_test_microphone(void); /* peak sample level, 0-32767, or -1 on error */
