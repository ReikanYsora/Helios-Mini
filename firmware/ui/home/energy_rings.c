#include "energy_rings.h"
#include "display.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

LV_IMAGE_DECLARE(mdi_home_icon);

/* Screen is 466x466 (see board_config.h) - three concentric rings, outer to
 * inner: solar / grid / battery, each 22px wide with an 8px gap between
 * them, leaving enough margin that nothing clips the round panel's edge. */
#define RING_WIDTH        22
#define RING_GAP          8
#define RING_SOLAR_DIAM   440
#define RING_GRID_DIAM    (RING_SOLAR_DIAM - 2 * (RING_WIDTH + RING_GAP))
#define RING_BATTERY_DIAM (RING_GRID_DIAM - 2 * (RING_WIDTH + RING_GAP))

static lv_obj_t *s_screen;
static lv_obj_t *s_arc_solar;
static lv_obj_t *s_arc_grid;
static lv_obj_t *s_arc_battery;
static lv_obj_t *s_center_icon;
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
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    /* lv_arc's default theme draws a small draggable "knob" circle at the
     * indicator's end - meant for interactive sliders, not a read-only
     * ring, and it showed up as a stray colored dot. lv_obj_remove_style()
     * alone doesn't reliably strip every theme-applied style, so every
     * visual property that could paint it is overridden explicitly and
     * its padding zeroed so it doesn't even reserve space. */
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_outline_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    /* No visible track - only the colored indicator is drawn. At 0% its
     * start and end angles coincide, and with rounded caps that zero-
     * length arc still renders as a small dot marking the ring's start,
     * rather than nothing at all. */
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, RING_WIDTH, LV_PART_INDICATOR);
    /* Rounded caps on the fill - the Apple Watch activity-ring look the
     * user asked for. */
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
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

    /* A house icon above the number stands in for a "home consumption"
     * label - no redundant text needed when everything's fine. */
    s_center_icon = lv_image_create(scr);
    lv_image_set_src(s_center_icon, &mdi_home_icon);
    lv_obj_align_to(s_center_icon, s_center_text, LV_ALIGN_OUT_TOP_MID, 0, -8);

    s_center_sub = lv_label_create(scr);
    lv_obj_set_style_text_color(s_center_sub, lv_color_hex(0x9a9aa5), 0);
    lv_label_set_text(s_center_sub, "waiting for data");
    lv_obj_align_to(s_center_sub, s_center_text, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    display_unlock();
}

/* Ring fill transitions are animated (a real lerp, via LVGL's own
 * animation timer - not a hard jump) so a new reading visibly sweeps into
 * place instead of snapping, the way an Apple Watch ring fills. */
#define RING_ANIM_MS 700

static void arc_anim_exec_cb(void *var, int32_t value)
{
    lv_arc_set_value((lv_obj_t *)var, value);
}

static void animate_arc_to(lv_obj_t *arc, int32_t target_value)
{
    int32_t current = lv_arc_get_value(arc);
    if (current == target_value) {
        return;
    }
    lv_anim_delete(arc, arc_anim_exec_cb); /* cancel any transition already in flight for this ring */

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arc_anim_exec_cb);
    lv_anim_set_values(&a, current, target_value);
    lv_anim_set_time(&a, RING_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
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
    int32_t target = (int32_t)(percent * 10.0f);
    /* A truly zero-length arc doesn't get drawn at all, rounded caps or
     * not - LVGL has nothing to sweep. Floor it to a hair over 1 degree
     * (in this 0-1000 range, ~2.78 per degree) so the ring's start (and,
     * since start and end coincide, its end) always shows as a small dot
     * instead of vanishing outright - true for both a genuine 0% and a
     * ring that isn't configured at all. */
    if (target < 3) {
        target = 3;
    }
    animate_arc_to(arc, target);
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
