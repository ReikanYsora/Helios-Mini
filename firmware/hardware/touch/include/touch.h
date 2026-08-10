#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Brings up the CST820 touch controller on the shared I2C bus.
 * i2c_bus_init() must have been called first. Normally called internally by
 * hardware/display (display_init()); exposed here so it can also be
 * exercised standalone during bring-up. */
void touch_init(void);

/* Polls the CST820 for a touch point. Returns true and fills x/y (raw
 * panel coordinates, 0..HELIOS_LCD_H_RES/V_RES) if a finger is currently
 * down, false otherwise. Safe to call from the LVGL indev read callback. */
bool touch_read(uint16_t *x, uint16_t *y);
