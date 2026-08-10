#pragma once

/* Starts a low-priority background task that periodically logs free heap,
 * PSRAM, and uptime. Precursor to the full on-screen diagnostics page
 * (spec Section 24), which lands in V0.4 as a UI overlay reading these
 * same figures. */
void diagnostics_start(void);
