#include "energy_rings.h"
#include "display.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

/* Screen is 466x466 (see board_config.h) - three concentric rings, outer to
 * inner: solar / grid / battery, each 22px wide with an 8px gap between
 * them, leaving enough margin that nothing clips the round panel's edge. */
#define RING_WIDTH        22
#define RING_GAP          8
#define RING_SOLAR_DIAM   440
#define RING_GRID_DIAM    (RING_SOLAR_DIAM - 2 * (RING_WIDTH + RING_GAP))
#define RING_BATTERY_DIAM (RING_GRID_DIAM - 2 * (RING_WIDTH + RING_GAP))

/* Unlit track color for every ring - dark enough to read as "off" against
 * the black background without disappearing entirely. */
#define RING_TRACK_COLOR lv_color_hex(0x26262f)

static lv_obj_t *s_screen;
static lv_obj_t *s_arc_solar;
static lv_obj_t *s_arc_grid;
static lv_obj_t *s_arc_battery;
static lv_obj_t *s_center_text;
static lv_obj_t *s_center_sub;

static lv_obj_t *create_ring(lv_obj_t *parent, int diameter)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270); /* 0% starts at 12 o'clock, fills clockwise */
    lv_arc_set_range(arc, 0, 1000); /* 0.1% steps for a smooth fill */
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, RING_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, RING_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, RING_TRACK_COLOR, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    return arc;
}

void energy_rings_show(void)
{
    if (!display_lock(1000)) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr); /* drop whatever was on screen (boot animation, IP notice) */
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_screen = scr;
    s_arc_solar = create_ring(scr, RING_SOLAR_DIAM);
    s_arc_grid = create_ring(scr, RING_GRID_DIAM);
    s_arc_battery = create_ring(scr, RING_BATTERY_DIAM);

    s_center_text = lv_label_create(scr);
    lv_obj_set_style_text_font(s_center_text, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_center_text, lv_color_white(), 0);
    lv_label_set_text(s_center_text, "--");
    lv_obj_align(s_center_text, LV_ALIGN_CENTER, 0, -8);

    s_center_sub = lv_label_create(scr);
    lv_obj_set_style_text_color(s_center_sub, lv_color_hex(0x9a9aa5), 0);
    lv_label_set_text(s_center_sub, "waiting for data");
    lv_obj_align_to(s_center_sub, s_center_text, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    display_unlock();
}

static void apply_ring(lv_obj_t *arc, const energy_ring_value_t *value)
{
    if (arc == NULL || value == NULL) {
        return;
    }
    float percent = value->visible ? value->percent : 0.0f;
    if (percent < 0.0f) {
        percent = 0.0f;
    } else if (percent > 100.0f) {
        percent = 100.0f;
    }
    lv_arc_set_value(arc, (int32_t)(percent * 10.0f));
    lv_obj_set_style_arc_color(arc, lv_color_hex(value->color_hex), LV_PART_INDICATOR);
}

void energy_rings_update(const energy_display_t *display)
{
    if (display == NULL || s_screen == NULL) {
        return; /* energy_rings_show() hasn't run yet */
    }
    if (!display_lock(1000)) {
        return;
    }

    apply_ring(s_arc_solar, &display->solar);
    apply_ring(s_arc_grid, &display->grid);
    apply_ring(s_arc_battery, &display->battery);

    lv_label_set_text(s_center_text, display->center_text);
    lv_label_set_text(s_center_sub, display->center_sub);
    lv_obj_align_to(s_center_sub, s_center_text, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    display_unlock();
}
