#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

/* Renders whatever's currently on screen to a standalone 24bpp BMP file in
 * memory - grabs the actual screen for /debug's "Screenshot" download
 * (documentation shots, bug reports), no cable or extra tooling needed.
 * out_buf is allocated in PSRAM (a 466x466 24bpp BMP is ~650KB, too big to
 * risk on the internal heap) - caller owns it and must free() it. Returns
 * false (leaves out_buf/out_len untouched) if the snapshot or allocation
 * failed. Takes display_lock() itself - don't call this already holding it. */
bool display_take_screenshot_bmp(uint8_t **out_buf, size_t *out_len);
