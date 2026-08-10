#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Brings up the QSPI AMOLED panel, initializes LVGL, registers a pointer
 * indev backed by hardware/touch, and starts the LVGL refresh task. Also
 * brings up the touch controller internally (touch_init()) - callers don't
 * need to call it separately. Safe to call once, from app_main(). */
void display_init(void);

/* Sets panel brightness, 0-255. Sent as a QSPI command to the panel
 * controller - this board has no PWM backlight GPIO. */
void display_set_brightness(uint8_t level);

/* Serializes access to the LVGL object tree from any task. Every call into
 * lvgl.h from outside the internal LVGL task must be wrapped in
 * display_lock()/display_unlock(). */
bool display_lock(int timeout_ms);
void display_unlock(void);
