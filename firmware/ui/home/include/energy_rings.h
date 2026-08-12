#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Pure LVGL renderer for the Apple-Watch-style energy rings screen: three
 * concentric lv_arc widgets (solar / grid / battery, outer to inner) plus
 * the current home consumption as big text in the middle. Knows nothing
 * about Home Assistant or entity configuration - that's helios/energy_model,
 * which polls the data and calls energy_rings_update(). Kept dumb on
 * purpose so this component only ever REQUIRES display. */

typedef struct {
    bool visible;       /* false = ring not drawn - the underlying feature
                          * (solar / grid / battery) isn't configured or the
                          * board has no such device, so there's nothing to
                          * show instead of a misleading empty ring. */
    float percent;      /* 0-100, already clamped by the caller. */
    uint32_t color_hex;  /* caller picks the color, e.g. grid ring is blue
                          * while importing and purple while exporting. */
} energy_ring_value_t;

typedef struct {
    energy_ring_value_t solar;    /* PV production vs. configured max. */
    energy_ring_value_t grid;     /* import OR export - one arc, whichever
                                    * direction is currently active. */
    energy_ring_value_t battery;  /* charge OR discharge - same idea. */
    char center_text[32];         /* e.g. "1.24 kW" - home consumption. */
    char center_sub[32];          /* small caption under the number, e.g.
                                    * "home consumption" or an error hint
                                    * when nothing is configured yet. */
} energy_display_t;

/* Builds the rings screen and shows it (replacing whatever else is on
 * screen - boot animation, the IP notice, ...). All rings start empty at
 * 0% until the first energy_rings_update(). Safe to call more than once. */
void energy_rings_show(void);

/* Updates ring fill/color/visibility and the center text in place - cheap,
 * meant to be called every time helios/energy_model polls fresh data.
 * No-op if energy_rings_show() hasn't run yet. */
void energy_rings_update(const energy_display_t *display);
