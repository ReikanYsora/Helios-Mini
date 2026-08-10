#pragma once

/* Runs the spec Section 15 boot sequence on the active LVGL screen: black
 * screen -> small central dot -> circular sweep -> Helios logo fade-in.
 * Call once, right after display_init(). Non-blocking: schedules LVGL
 * animations and returns immediately; the sequence plays out over
 * subsequent ticks of the display component's own LVGL task.
 *
 * Wi-Fi/Home Assistant connection status (steps 6-7 of the spec sequence)
 * are not wired up yet - that needs networking/wifi and helios/ha_client,
 * which land in V0.2. */
void boot_animation_start(void);
