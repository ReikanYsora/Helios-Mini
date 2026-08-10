#pragma once

/* Must be called as early as possible in app_main(), before anything else.
 *
 * The board powers on via a physical PWR-button + latch circuit: pressing
 * the button turns the rail on and lets the MCU boot, but the rail turns
 * back off once the button is released unless firmware asserts this GPIO.
 * See docs/HARDWARE_REFERENCE.md. */
void power_latch_on(void);

/* Releases the power latch, letting the board power off. */
void power_latch_off(void);
