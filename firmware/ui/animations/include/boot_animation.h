#pragma once

/* Runs the boot sequence on the active LVGL screen: the Helios logo draws
 * itself in, its 12 sun-flame rays lighting up one by one around the
 * circle followed by the central disc (assets/helios_logo_pieces.h),
 * instead of a generic spinner. Call once, right after display_init().
 * Non-blocking: schedules LVGL animations and returns immediately; the
 * sequence plays out over subsequent ticks of the display component's own
 * LVGL task. */
void boot_animation_start(void);
