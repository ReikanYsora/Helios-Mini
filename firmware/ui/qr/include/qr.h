#pragma once

#include "lvgl.h"

/* Builds a scannable QR - black modules on a white quiet-zone card, so it
 * reads against the device's dark UI - for `data`, `size` px square, as a
 * child of parent. The caller positions it. */
lv_obj_t *helios_qr_create(lv_obj_t *parent, const char *data, int size);
