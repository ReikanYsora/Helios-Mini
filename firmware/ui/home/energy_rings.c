#include "energy_rings.h"
#include "display.h"
#include "lvgl.h"
#include "qr.h"
#include "figtree.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

LV_IMAGE_DECLARE(mdi_home_icon);
LV_IMAGE_DECLARE(mdi_weather_sunny);
LV_IMAGE_DECLARE(mdi_transmission_tower);
LV_IMAGE_DECLARE(mdi_battery_high);
LV_IMAGE_DECLARE(mdi_wifi);

/* Screen is 466x466 (see board_config.h). Up to six swipeable pages,
 * built on an lv_tileview: the Apple-Watch-style general view (four
 * concentric rings, outer to inner: irradiance / solar / grid / battery,
 * plus home consumption in the middle - unchanged from before this became
 * a tileview, always shown), then one dedicated "hero" view per source,
 * and a consumption view with a contributor breakdown - each of those
 * five independently togglable from /display (energy_pages_t), general
 * stays mandatory. Touch is already wired up in hardware/display -
 * lv_tileview handles the swipe gestures and paging entirely on its own
 * once tiles are added. */
/* Diameters kept small enough that even the outermost ring's bottom edge
 * clears the page-dot legend (bottom-aligned, -28px from the screen edge)
 * with a real margin - the rings used to run right underneath it. Width/gap
 * trimmed from 22/8 to fit a 4th ring without crowding the center text -
 * the innermost ring's clear diameter only drops from 196 to 180px. */
#define RING_WIDTH            18
#define RING_GAP               6
#define RING_IRRADIANCE_DIAM  360
#define RING_SOLAR_DIAM       (RING_IRRADIANCE_DIAM - 2 * (RING_WIDTH + RING_GAP))
#define RING_GRID_DIAM        (RING_SOLAR_DIAM - 2 * (RING_WIDTH + RING_GAP))
#define RING_BATTERY_DIAM     (RING_GRID_DIAM - 2 * (RING_WIDTH + RING_GAP))

/* Apple-Watch activity-ring look: a full dim groove behind every ring (the
 * ring's own colour knocked back toward black) with the bright fill riding
 * over it, and a small lighter-shade disc marking the fill's leading tip -
 * the tip marker Apple draws an arrow glyph in, a plain bead here. */
#define RING_TIP_DIAM     (RING_WIDTH + 2)
#define RING_TRACK_MIX     70   /* groove = ~27% ring colour over black */
#define RING_TIP_MIX      180   /* tip disc = ring colour lifted ~30% toward white */

/* The general view's centre number must never spill onto the innermost
 * ring: clamp it to the clear disc inside the battery ring and step the
 * Figtree size down (48 -> 32 -> 24) until it fits that width. */
#define CENTER_TEXT_MAX_W  (RING_BATTERY_DIAM - 2 * RING_WIDTH - 12)

/* Stroke centreline radius for a ring of the given outer diameter - where
 * its tip bead rides. The bead disc is reachable from the arc through its
 * user_data, so no per-ring bookkeeping struct is needed. */
#define RING_CENTERLINE_R(arc)  (lv_obj_get_width(arc) / 2 - RING_WIDTH / 2)

/* Hero views (solar/grid/battery) borrow Helios's own HUD-chip visual
 * language instead of a ring: a pill chip (icon + value, 2px border in
 * the metric's colour, card-background fill) fed by a dashed "lead" line
 * from above with a small "beam" riding it toward the chip, speed
 * proportional to the live reading - the same chip/leader/bead grammar
 * the parent Helios card itself uses (chip-appearance.ts,
 * scene-hud-controller.ts). Screen center is (233, 233); geometry below
 * is fixed in absolute tile coordinates so it doesn't shift with the
 * chip's content width. */
#define SCREEN_CENTER_X     233
#define SCREEN_CENTER_Y     233
#define CHIP_HEIGHT          56
/* Fixed, not content-hugging - like Helios's own chips ("Fixed width so
 * every chip is identical; content centres within it.", scene-css). Sized
 * for the longest realistic reading ("12.345 kW" at 3 decimals) plus icon
 * and padding, so solar/grid/battery chips all read as the same shape
 * regardless of value length. */
#define CHIP_WIDTH           248
#define CHIP_ICON_SIZE        28
#define CHIP_GAP               8
#define CHIP_PAD_H            18
#define CHIP_BORDER_W          3
#define CHIP_Y_OFFSET        (-20) /* from screen center - leaves room below for the caption and the page dots */
#define CHIP_TOP_Y  (SCREEN_CENTER_Y + CHIP_Y_OFFSET - CHIP_HEIGHT / 2)
#define CHIP_SUB_Y  (SCREEN_CENTER_Y + CHIP_Y_OFFSET + CHIP_HEIGHT / 2 + 18)
#define LEAD_TOP_Y            58   /* near the round screen's true top point (x=233 is where the circle touches y=0) */
#define LEAD_DASH_WIDTH        4
#define LEAD_DASH_GAP           5
#define BEAM_DIAM               8
/* Bead speed scales with the live reading, same idea as Helios's own
 * leader beads - fast at a strong reading, a lazy drift at a trickle. */
#define BEAM_ANIM_MS_SLOW    2400
#define BEAM_ANIM_MS_FAST     900
#define ICON_PULSE_MS         220  /* quick blink when the beam reaches the chip - dip to 50% opa and back, never fully hidden */
#define NEUTRAL_COLOR_HEX 0x46464f /* not-configured chip/lead colour - same grey as an inactive page dot */

/* Array capacity, not a fixed page count any more - which of the 5
 * dedicated pages actually get built (and thus which slots below are
 * populated) is decided at energy_rings_show() time from energy_pages_t,
 * tracked at runtime in s_page_count. */
#define MAX_PAGES 6

/* One "hero" (chip + lead + beam + caption) view - shared structure for
 * the solar/grid/battery dedicated pages, so the three only differ by
 * which icon and which energy_ring_value_t drives them. */
typedef struct {
    lv_obj_t *chip;
    lv_obj_t *icon;
    lv_obj_t *value_label;
    lv_obj_t *sub_label;
    lv_obj_t *lead;
    lv_obj_t *beam;
    lv_point_precise_t lead_points[2]; /* lv_line only stores the pointer - must outlive the widget */
    bool beam_running;
    /* The beam runs as a chain of one-shot "legs" (top -> chip) rather than
     * one LV_ANIM_REPEAT_INFINITE animation, so each arrival is a real
     * event (completed_cb) that can trigger the icon pulse - infinite
     * repeat never fires a completed callback per cycle, only on final
     * deletion. beam_duration_ms is read fresh by each leg's completion
     * handler, so update_hero_view() can retune the speed without ever
     * interrupting a leg already in flight (no visible jump). */
    uint32_t beam_duration_ms;
} hero_view_t;

static lv_obj_t *s_tileview;
static lv_obj_t *s_tiles[MAX_PAGES];
static lv_obj_t *s_dots[MAX_PAGES];
static lv_obj_t *s_dots_row;
static int s_page_count; /* how many of the MAX_PAGES slots above are actually populated */

/* One vertical neighbor of the general view: swipe up for the IP screen
 * (row 0), general itself stays at row 1 (its default). A left-edge dot
 * column (s_vdots), mirroring the horizontal page dots at the bottom,
 * shows position on this axis instead of lv_tileview's own default
 * scrollbar. */
static lv_obj_t *s_ip_tile;
static lv_obj_t *s_ip_label;   /* single line, "192.168.0.45" */
static lv_obj_t *s_ip_qr;      /* QR of the settings URL, hidden until an IP is known */
static lv_obj_t *s_vdots_row;
static lv_obj_t *s_vdots[2]; /* [0]=general (top), [1]=IP (bottom) - matches row order */

/* Which dedicated pages the current screen was built with - gates
 * energy_rings_update() so it never touches a hero/consumption view whose
 * widgets weren't built (page toggled off) or were torn down by a
 * previous energy_rings_show() rebuild. */
static bool s_show_solar;
static bool s_show_irradiance;
static bool s_show_grid;
static bool s_show_battery;
static bool s_show_consumption;

/* General view widgets. */
static lv_obj_t *s_arc_solar;
static lv_obj_t *s_arc_irradiance;
static lv_obj_t *s_arc_grid;
static lv_obj_t *s_arc_battery;
static lv_obj_t *s_center_icon;
static lv_obj_t *s_center_text;
static lv_obj_t *s_center_sub;

static hero_view_t s_solar_view;
static hero_view_t s_irradiance_view;
static hero_view_t s_grid_view;
static hero_view_t s_battery_view;

/* Consumption view: its own big number/icon (separate objects from the
 * general view's, even though they show the same text - each page owns
 * its widgets outright, simplest way to keep two independent lv_tileview
 * tiles in sync). */
static lv_obj_t *s_consumption_icon;
static lv_obj_t *s_consumption_text;
static lv_obj_t *s_consumption_sub;
static lv_obj_t *s_breakdown_solar;
static lv_obj_t *s_breakdown_grid_import;
static lv_obj_t *s_breakdown_grid_export;
static lv_obj_t *s_breakdown_battery_charge;
static lv_obj_t *s_breakdown_battery_discharge;

static lv_obj_t *create_ring(lv_obj_t *parent, int diameter, int width)
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

    /* Dim groove behind the fill (colour set per-ring at update time), the
     * full-circle track the bright indicator rides over. bg_opa stays off -
     * that's the widget's rectangle, not the ring. */
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    /* Rounded caps on the fill - the Apple Watch activity-ring look. At 0%
     * start and end coincide and the rounded cap still shows a small dot. */
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    /* Leading-tip bead: a small disc pinned to the fill's front edge,
     * repositioned as the value animates (arc_anim_exec_cb). Sits on the
     * stroke centreline, starting at 12 o'clock where a 0% fill begins.
     * Stashed on the arc's user_data so both the animation and the colour
     * update can reach it - and torn down with the arc by lv_obj_clean(). */
    lv_obj_t *disc = lv_obj_create(parent);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, RING_TIP_DIAM, RING_TIP_DIAM);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(disc, LV_ALIGN_CENTER, 0, -(diameter / 2 - width / 2));
    lv_obj_set_user_data(arc, disc);
    return arc;
}

/* Ring fill transitions are animated (a real lerp, via LVGL's own
 * animation timer - not a hard jump) so a new reading visibly sweeps into
 * place instead of snapping, the way an Apple Watch ring fills. */
#define RING_ANIM_MS 700

/* Pin a ring's tip bead to the fill's leading edge. 0% starts at 12
 * o'clock (rotation 270) and fills clockwise, so the angle is 270deg +
 * value's share of the full turn, and the offset from centre lands the
 * bead on the stroke centreline. */
static void position_tip_disc(lv_obj_t *arc)
{
    lv_obj_t *disc = lv_obj_get_user_data(arc);
    if (disc == NULL) {
        return;
    }
    int32_t r = RING_CENTERLINE_R(arc);
    float ang = (270.0f + lv_arc_get_value(arc) / 1000.0f * 360.0f) * 0.017453293f;
    int32_t dx = (int32_t)lroundf(r * cosf(ang));
    int32_t dy = (int32_t)lroundf(r * sinf(ang));
    lv_obj_align(disc, LV_ALIGN_CENTER, dx, dy);
}

static void arc_anim_exec_cb(void *var, int32_t value)
{
    lv_arc_set_value((lv_obj_t *)var, value);
    position_tip_disc((lv_obj_t *)var);
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

static int32_t ring_target_value(const energy_ring_value_t *value, bool visible)
{
    float percent = visible ? value->percent : 0.0f;
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
    return target;
}

/* Applies one metric colour across a ring's three parts: bright fill,
 * dimmed groove, and lighter tip bead. */
static void set_ring_colors(lv_obj_t *arc, uint32_t hex)
{
    lv_color_t c = lv_color_hex(hex);
    lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_mix(c, lv_color_black(), RING_TRACK_MIX), LV_PART_MAIN);
    lv_obj_t *disc = lv_obj_get_user_data(arc);
    if (disc != NULL) {
        lv_obj_set_style_bg_color(disc, lv_color_mix(c, lv_color_white(), RING_TIP_MIX), 0);
    }
}

/* Sets the centre number at the largest Figtree size that still fits the
 * clear disc inside the rings, so a long value (big wattage, or kW with a
 * decimal) shrinks instead of running under the innermost ring. */
static void fit_center_text(lv_obj_t *label, const char *text)
{
    static const lv_font_t *const fonts[] = { &figtree_48, &figtree_32, &figtree_24 };
    size_t count = sizeof(fonts) / sizeof(fonts[0]);
    const lv_font_t *chosen = fonts[count - 1];
    for (size_t i = 0; i < count; i++) {
        lv_point_t size;
        lv_text_get_size(&size, text, fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= CENTER_TEXT_MAX_W) {
            chosen = fonts[i];
            break;
        }
    }
    lv_obj_set_style_text_font(label, chosen, 0);
    lv_label_set_text(label, text);
}

/* ---- general view (page 0) ---- */

static void build_general_view(lv_obj_t *tile)
{
    s_arc_irradiance = create_ring(tile, RING_IRRADIANCE_DIAM, RING_WIDTH);
    s_arc_solar = create_ring(tile, RING_SOLAR_DIAM, RING_WIDTH);
    s_arc_grid = create_ring(tile, RING_GRID_DIAM, RING_WIDTH);
    s_arc_battery = create_ring(tile, RING_BATTERY_DIAM, RING_WIDTH);

    s_center_text = lv_label_create(tile);
    lv_obj_set_style_text_font(s_center_text, &figtree_48, 0);
    lv_obj_set_style_text_color(s_center_text, lv_color_white(), 0);
    lv_label_set_text(s_center_text, "--");
    lv_obj_align(s_center_text, LV_ALIGN_CENTER, 0, -8);

    /* A house icon above the number stands in for a "home consumption"
     * label - no redundant text needed when everything's fine. */
    s_center_icon = lv_image_create(tile);
    lv_image_set_src(s_center_icon, &mdi_home_icon);
    lv_obj_align_to(s_center_icon, s_center_text, LV_ALIGN_OUT_TOP_MID, 0, -8);

    s_center_sub = lv_label_create(tile);
    lv_obj_set_style_text_color(s_center_sub, lv_color_hex(0x9a9aa5), 0);
    lv_label_set_text(s_center_sub, "");
    lv_obj_align_to(s_center_sub, s_center_text, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

static void update_general_view(const energy_display_t *d)
{
    animate_arc_to(s_arc_irradiance, ring_target_value(&d->irradiance, d->irradiance.visible));
    set_ring_colors(s_arc_irradiance, d->irradiance.color_hex);

    animate_arc_to(s_arc_solar, ring_target_value(&d->solar, d->solar.visible));
    set_ring_colors(s_arc_solar, d->solar.color_hex);

    animate_arc_to(s_arc_grid, ring_target_value(&d->grid, d->grid.visible));
    set_ring_colors(s_arc_grid, d->grid.color_hex);

    animate_arc_to(s_arc_battery, ring_target_value(&d->battery, d->battery.visible));
    set_ring_colors(s_arc_battery, d->battery.color_hex);

    fit_center_text(s_center_text, d->center_text);
    lv_label_set_text(s_center_sub, d->center_sub);
    lv_obj_align_to(s_center_sub, s_center_text, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

/* ---- hero views (pages 1-3: solar / grid / battery) ---- */

static void beam_anim_exec_cb(void *var, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)var, y);
}

static void icon_opa_exec_cb(void *var, int32_t opa)
{
    lv_obj_set_style_image_opa((lv_obj_t *)var, (lv_opa_t)opa, 0);
}

/* Tiny arrival feedback: the chip's icon dips to 50% and back to full -
 * never fully hidden, just a blink - each time the beam reaches the chip. */
static void pulse_icon(lv_obj_t *icon)
{
    lv_anim_delete(icon, icon_opa_exec_cb); /* in case a previous pulse hasn't finished (a fast beam) */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, icon);
    lv_anim_set_exec_cb(&a, icon_opa_exec_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_50);
    lv_anim_set_time(&a, ICON_PULSE_MS / 2);
    lv_anim_set_playback_time(&a, ICON_PULSE_MS / 2); /* dip then back up - a blink, not a fade-out */
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void start_beam_leg(lv_obj_t *beam, uint32_t duration_ms);

/* Fires once per leg, when the beam reaches the chip - completed_cb only
 * fires on LV_ANIM_REPEAT_INFINITE at final deletion, never per cycle, so
 * the beam runs as a self-chaining series of one-shot legs instead (see
 * hero_view_t's comment) purely to get a real "arrived" event out of it. */
static void beam_leg_completed_cb(lv_anim_t *a)
{
    lv_obj_t *beam = (lv_obj_t *)a->var;
    hero_view_t *v = (hero_view_t *)lv_obj_get_user_data(beam);
    if (v == NULL) {
        return;
    }
    pulse_icon(v->icon);
    if (v->beam_running) {
        start_beam_leg(beam, v->beam_duration_ms); /* speed already kept current by update_hero_view() */
    }
}

static void start_beam_leg(lv_obj_t *beam, uint32_t duration_ms)
{
    lv_anim_delete(beam, beam_anim_exec_cb);
    lv_obj_set_y(beam, LEAD_TOP_Y - BEAM_DIAM / 2); /* every leg starts fresh from the top */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, beam);
    lv_anim_set_exec_cb(&a, beam_anim_exec_cb);
    lv_anim_set_values(&a, LEAD_TOP_Y - BEAM_DIAM / 2, CHIP_TOP_Y - BEAM_DIAM / 2);
    lv_anim_set_time(&a, duration_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, beam_leg_completed_cb);
    lv_anim_start(&a);
}

static void stop_beam(lv_obj_t *beam)
{
    lv_anim_delete(beam, beam_anim_exec_cb); /* explicit delete never fires completed_cb - no stray chaining */
}

/* Bead duration for a given ring percent (0-100): fast at a strong
 * reading, a lazy drift at a trickle - see BEAM_ANIM_MS_* above. */
static uint32_t beam_duration_for_percent(float percent)
{
    if (percent < 0.0f) {
        percent = 0.0f;
    } else if (percent > 100.0f) {
        percent = 100.0f;
    }
    return (uint32_t)(BEAM_ANIM_MS_SLOW - (percent / 100.0f) * (BEAM_ANIM_MS_SLOW - BEAM_ANIM_MS_FAST));
}

/* Fills in *v in place (rather than returning by value) because the beam's
 * completed_cb needs a stable pointer back to its own hero_view_t - taking
 * the address of a by-value return's local copy would dangle the moment
 * this function returned. Callers pass the address of their static
 * s_solar_view/s_grid_view/s_battery_view, which live for the process. */
static void build_hero_view(lv_obj_t *tile, const lv_image_dsc_t *icon_src, hero_view_t *v)
{
    *v = (hero_view_t){0};

    /* Lead: a dashed vertical line from near the top of the round screen
     * down to the chip, in the metric's colour - Helios's own chip
     * leaders, borrowed wholesale for this full-screen page. Explicitly
     * sized/positioned rather than relying on lv_line's own content-based
     * self-sizing: that left the line rendering off to the left on real
     * hardware (and its default-scrollable, tile-sized empty box was a
     * plausible source of the intermittent swipe hiccups reported
     * alongside it) instead of the vertical line at screen-center x=233
     * the two absolute points below describe. */
    v->lead_points[0] = (lv_point_precise_t){ SCREEN_CENTER_X, LEAD_TOP_Y };
    v->lead_points[1] = (lv_point_precise_t){ SCREEN_CENTER_X, CHIP_TOP_Y };
    v->lead = lv_line_create(tile);
    lv_obj_set_pos(v->lead, 0, 0);
    lv_obj_set_size(v->lead, SCREEN_CENTER_X * 2, CHIP_TOP_Y);
    lv_line_set_points(v->lead, v->lead_points, 2);
    lv_obj_set_style_line_width(v->lead, 2, 0);
    lv_obj_set_style_line_rounded(v->lead, true, 0);
    lv_obj_set_style_line_dash_width(v->lead, LEAD_DASH_WIDTH, 0);
    lv_obj_set_style_line_dash_gap(v->lead, LEAD_DASH_GAP, 0);
    lv_obj_remove_flag(v->lead, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->lead, LV_OBJ_FLAG_SCROLLABLE); /* purely decorative - never eligible to eat the tileview's swipe */

    /* Chip: Helios's own HUD-chip recipe (helios-card-scene-css.ts) - a
     * pill, 2px border in the metric colour, background matching the
     * device's black theme, icon + bold value, FIXED width so all three
     * hero chips read as the same shape - scaled up for a full-screen
     * dedicated page instead of a small map overlay. */
    v->chip = lv_obj_create(tile);
    lv_obj_remove_style_all(v->chip);
    lv_obj_set_size(v->chip, CHIP_WIDTH, CHIP_HEIGHT);
    lv_obj_set_style_radius(v->chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(v->chip, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(v->chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(v->chip, CHIP_BORDER_W, 0);
    lv_obj_set_style_pad_hor(v->chip, CHIP_PAD_H, 0);
    lv_obj_set_flex_flow(v->chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(v->chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(v->chip, CHIP_GAP, 0);
    lv_obj_remove_flag(v->chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(v->chip, LV_ALIGN_CENTER, 0, CHIP_Y_OFFSET);

    v->icon = lv_image_create(v->chip);
    lv_image_set_src(v->icon, icon_src);
    /* Recolored at update time to match the chip's current color (import
     * vs export, charge vs discharge, ...) - rasterized in plain white so
     * one asset can be tinted to any of them. */
    lv_obj_set_style_image_recolor_opa(v->icon, LV_OPA_COVER, 0);

    v->value_label = lv_label_create(v->chip);
    lv_obj_set_style_text_font(v->value_label, &figtree_32, 0);
    lv_obj_set_style_text_color(v->value_label, lv_color_white(), 0);
    lv_label_set_text(v->value_label, "--");

    /* Beam: a small filled disc riding the lead toward the chip, same
     * idea as the moving bead on Helios's own leaders. Hidden (no anim)
     * whenever there's nothing really flowing to show - see
     * update_hero_view(). user_data points back to this hero_view_t so
     * beam_leg_completed_cb can reach the icon (for the arrival pulse) and
     * the live beam_duration_ms (for the next leg). */
    v->beam = lv_obj_create(tile);
    lv_obj_remove_style_all(v->beam);
    lv_obj_set_size(v->beam, BEAM_DIAM, BEAM_DIAM);
    lv_obj_set_style_radius(v->beam, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(v->beam, LV_OPA_COVER, 0);
    lv_obj_remove_flag(v->beam, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->beam, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(v->beam, SCREEN_CENTER_X - BEAM_DIAM / 2, LEAD_TOP_Y - BEAM_DIAM / 2);
    lv_obj_add_flag(v->beam, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(v->beam, v);

    v->sub_label = lv_label_create(tile);
    lv_obj_set_style_text_color(v->sub_label, lv_color_hex(0x9a9aa5), 0);
    lv_label_set_text(v->sub_label, "");
    lv_obj_align(v->sub_label, LV_ALIGN_CENTER, 0, CHIP_SUB_Y - SCREEN_CENTER_Y);
}

/* live_label is shown while status is LIVE/STALE and the source has no
 * direction of its own (solar) - grid/battery pass NULL and rely on
 * value->direction_label ("Import"/"Charge"/...) instead. */
static void update_hero_view(hero_view_t *v, const energy_ring_value_t *value, const char *live_label)
{
    bool live = (value->status == ENERGY_RING_LIVE || value->status == ENERGY_RING_STALE);
    bool configured = value->status != ENERGY_RING_NOT_CONFIGURED;
    lv_color_t color = configured ? lv_color_hex(value->color_hex) : lv_color_hex(NEUTRAL_COLOR_HEX);

    lv_obj_set_style_border_color(v->chip, color, 0);
    lv_obj_set_style_line_color(v->lead, color, 0);
    lv_obj_set_style_image_recolor(v->icon, color, 0);
    lv_obj_set_style_bg_color(v->beam, color, 0);

    lv_label_set_text(v->value_label, live ? value->value_text : "--");

    const char *sub;
    if (value->status == ENERGY_RING_NOT_CONFIGURED) {
        sub = "Not configured";
    } else if (value->status == ENERGY_RING_WAITING) {
        sub = "Waiting for data";
    } else if (value->direction_label[0] != '\0') {
        sub = value->direction_label;
    } else if (live_label != NULL) {
        sub = live_label;
    } else {
        sub = "";
    }
    if (value->status == ENERGY_RING_STALE) {
        lv_label_set_text_fmt(v->sub_label, "%s (stale)", sub);
    } else {
        lv_label_set_text(v->sub_label, sub);
    }

    /* Speed is always kept current: a leg already in flight picks up the
     * new number at its next arrival (beam_leg_completed_cb reads
     * beam_duration_ms fresh) rather than being yanked back to the top by
     * a forced restart every 3s poll. */
    v->beam_duration_ms = beam_duration_for_percent(value->percent);

    /* Beam only runs on a genuinely live, meaningfully-nonzero reading -
     * a configured-but-idle or stale source keeps its chip and dashed
     * lead but no misleading motion, same grammar as Helios's own idle
     * leaders. */
    bool should_run = (value->status == ENERGY_RING_LIVE) && value->percent > 0.5f;
    if (should_run && !v->beam_running) {
        start_beam_leg(v->beam, v->beam_duration_ms);
        v->beam_running = true;
        lv_obj_remove_flag(v->beam, LV_OBJ_FLAG_HIDDEN);
    } else if (!should_run && v->beam_running) {
        stop_beam(v->beam);
        v->beam_running = false;
        lv_obj_add_flag(v->beam, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- consumption view (page 4) ---- */

static lv_obj_t *create_breakdown_row(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, lv_color_hex(0xc8c8d0), 0);
    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}

static void set_breakdown_row(lv_obj_t *label, const char *text)
{
    if (text == NULL || text[0] == '\0') {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

static void build_consumption_view(lv_obj_t *tile)
{
    s_consumption_text = lv_label_create(tile);
    lv_obj_set_style_text_font(s_consumption_text, &figtree_48, 0);
    lv_obj_set_style_text_color(s_consumption_text, lv_color_white(), 0);
    lv_label_set_text(s_consumption_text, "--");
    lv_obj_align(s_consumption_text, LV_ALIGN_CENTER, 0, -110);

    s_consumption_icon = lv_image_create(tile);
    lv_image_set_src(s_consumption_icon, &mdi_home_icon);
    lv_obj_align_to(s_consumption_icon, s_consumption_text, LV_ALIGN_OUT_TOP_MID, 0, -8);

    s_consumption_sub = lv_label_create(tile);
    lv_obj_set_style_text_color(s_consumption_sub, lv_color_hex(0x9a9aa5), 0);
    lv_label_set_text(s_consumption_sub, "");
    lv_obj_align_to(s_consumption_sub, s_consumption_text, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    /* A small breakdown of what's actually feeding into the number above -
     * the one thing this dedicated page can show that the general view's
     * compact center text can't. Only sources that are actually
     * configured and live show up here at all. */
    lv_obj_t *list = lv_obj_create(tile);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 260, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 90);

    s_breakdown_solar = create_breakdown_row(list);
    s_breakdown_grid_import = create_breakdown_row(list);
    s_breakdown_grid_export = create_breakdown_row(list);
    s_breakdown_battery_charge = create_breakdown_row(list);
    s_breakdown_battery_discharge = create_breakdown_row(list);
}

static void update_consumption_view(const energy_display_t *d)
{
    lv_label_set_text(s_consumption_text, d->center_text);
    lv_label_set_text(s_consumption_sub, d->center_sub);
    lv_obj_align_to(s_consumption_sub, s_consumption_text, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    set_breakdown_row(s_breakdown_solar, d->breakdown_solar);
    set_breakdown_row(s_breakdown_grid_import, d->breakdown_grid_import);
    set_breakdown_row(s_breakdown_grid_export, d->breakdown_grid_export);
    set_breakdown_row(s_breakdown_battery_charge, d->breakdown_battery_charge);
    set_breakdown_row(s_breakdown_battery_discharge, d->breakdown_battery_discharge);
}

/* ---- IP screen (vertical swipe up from the general view) ---- */

static void build_ip_view(lv_obj_t *tile)
{
    lv_obj_t *stack = lv_obj_create(tile);
    lv_obj_remove_style_all(stack);
    lv_obj_set_size(stack, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(stack, 10, 0);
    lv_obj_center(stack);

    lv_obj_t *icon = lv_image_create(stack);
    lv_image_set_src(icon, &mdi_wifi);

    s_ip_label = lv_label_create(stack);
    /* Same size as a hero chip's value text, per spec. */
    lv_obj_set_style_text_font(s_ip_label, &figtree_32, 0);
    lv_obj_set_style_text_color(s_ip_label, lv_color_white(), 0);
    lv_label_set_text(s_ip_label, "--");

    s_ip_qr = helios_qr_create(stack, "http://0.0.0.0/", 140);
    lv_obj_add_flag(s_ip_qr, LV_OBJ_FLAG_HIDDEN);
}

void energy_rings_set_ip(const char *ip)
{
    if (s_ip_tile == NULL) {
        return; /* energy_rings_show() hasn't run yet */
    }
    if (!display_lock(1000)) {
        return;
    }
    if (ip != NULL && ip[0] != '\0') {
        lv_label_set_text(s_ip_label, ip);
        char url[40];
        snprintf(url, sizeof(url), "http://%s/", ip);
        lv_qrcode_update(s_ip_qr, url, (uint32_t)strlen(url));
        lv_obj_remove_flag(s_ip_qr, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_ip_label, "--");
        lv_obj_add_flag(s_ip_qr, LV_OBJ_FLAG_HIDDEN);
    }
    display_unlock();
}

/* ---- page indicator dots, overlaid on top of the tileview ---- */

static void update_dots(uint32_t active_index)
{
    for (int i = 0; i < s_page_count; i++) {
        bool active = ((uint32_t)i == active_index);
        lv_obj_set_style_bg_color(s_dots[i], active ? lv_color_white() : lv_color_hex(0x46464f), 0);
        lv_obj_set_size(s_dots[i], active ? 8 : 6, active ? 8 : 6);
    }
}

/* Same idea as update_dots() but for the left-edge vertical axis (general /
 * IP - see the s_vdots comment). index is a row index: 0=general (top),
 * 1=IP (bottom). */
static void update_vdots(int index)
{
    for (int i = 0; i < 2; i++) {
        bool active = (i == index);
        lv_obj_set_style_bg_color(s_vdots[i], active ? lv_color_white() : lv_color_hex(0x46464f), 0);
        lv_obj_set_size(s_vdots[i], active ? 8 : 6, active ? 8 : 6);
    }
}

static void tileview_event_cb(lv_event_t *e)
{
    lv_obj_t *tv = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *active_tile = lv_tileview_get_tile_active(tv);

    /* The IP screen is a vertical neighbor of the general view, not part of
     * the horizontal s_tiles[] row - the horizontal dots indicator only
     * means something there, so hide it on the IP screen and show the
     * vertical one's top/bottom position instead. */
    if (active_tile == s_ip_tile) {
        lv_obj_add_flag(s_dots_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_vdots_row, LV_OBJ_FLAG_HIDDEN);
        update_vdots(1);
        return;
    }
    lv_obj_remove_flag(s_dots_row, LV_OBJ_FLAG_HIDDEN);

    /* The general<->IP vertical axis only exists at the general view, so its
     * left-edge dots show there (and on the IP screen it leads to) and stay
     * hidden on every hero/consumption page, which has no up/down neighbour
     * for them to mean anything. */
    if (active_tile == s_tiles[0]) {
        lv_obj_remove_flag(s_vdots_row, LV_OBJ_FLAG_HIDDEN);
        update_vdots(0);
    } else {
        lv_obj_add_flag(s_vdots_row, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < s_page_count; i++) {
        if (s_tiles[i] == active_tile) {
            update_dots((uint32_t)i);
            break;
        }
    }
}

/* Adds the next tile in sequence (col_id = s_page_count, so tiles must be
 * added in final left-to-right order) with the right swipe directions for
 * its position: only the very first tile can be reached going right, only
 * the very last going left, everything in between both ways - independent
 * of how many of the 5 possible pages actually made the cut this time.
 * total is the final page count, known up front by energy_rings_show()
 * before any tile is added. All tiles here sit in row 0 - the general
 * view (always n==0) additionally has a vertical neighbor in row 1 (IP,
 * below) - see energy_rings_show(). */
static lv_obj_t *add_next_tile(int total)
{
    int n = s_page_count;
    lv_dir_t dir = (total <= 1) ? LV_DIR_NONE
                 : (n == 0) ? LV_DIR_RIGHT
                 : (n == total - 1) ? LV_DIR_LEFT
                 : LV_DIR_HOR;
    if (n == 0) {
        dir |= LV_DIR_BOTTOM;
    }
    lv_obj_t *tile = lv_tileview_add_tile(s_tileview, n, 0, dir);
    lv_obj_set_style_bg_color(tile, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    s_tiles[n] = tile;
    s_page_count++;
    return tile;
}

void energy_rings_show(const energy_pages_t *pages)
{
    static const energy_pages_t all_shown = { true, true, true, true, true };
    if (pages == NULL) {
        pages = &all_shown;
    }

    if (!display_lock(1000)) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr); /* drop whatever was on screen (boot animation, IP notice, a previous call's screen) */
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_tileview = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(s_tileview, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_COVER, 0);
    /* Custom dot indicators (below and on the left) replace lv_tileview's
     * own default scrollbar entirely - see s_dots_row/s_vdots_row. */
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);

    s_show_solar = pages->solar;
    s_show_irradiance = pages->irradiance;
    s_show_grid = pages->grid;
    s_show_battery = pages->battery;
    s_show_consumption = pages->consumption;
    int total = 1 /* general - always shown */
              + (s_show_solar ? 1 : 0)
              + (s_show_irradiance ? 1 : 0)
              + (s_show_grid ? 1 : 0)
              + (s_show_battery ? 1 : 0)
              + (s_show_consumption ? 1 : 0);
    s_page_count = 0;

    lv_obj_t *general_tile = add_next_tile(total);
    build_general_view(general_tile);
    if (s_show_irradiance) {
        /* Same glyph as production - Helios's own chip-appearance.ts defaults
         * both slots to mdi:weather-sunny too, differentiated by colour
         * (amber vs orange) and, here, by being a whole separate page. First
         * dedicated page: what the sun is doing, before what it's producing. */
        build_hero_view(add_next_tile(total), &mdi_weather_sunny, &s_irradiance_view);
    }
    if (s_show_solar) {
        build_hero_view(add_next_tile(total), &mdi_weather_sunny, &s_solar_view);
    }
    if (s_show_grid) {
        build_hero_view(add_next_tile(total), &mdi_transmission_tower, &s_grid_view);
    }
    if (s_show_battery) {
        build_hero_view(add_next_tile(total), &mdi_battery_high, &s_battery_view);
    }
    if (s_show_consumption) {
        build_consumption_view(add_next_tile(total));
    }

    /* IP screen: vertical neighbor below the general view (row 1), reached
     * by swiping up from it, back down to return. LV_DIR_TOP here pairs
     * with the LV_DIR_BOTTOM add_next_tile() gave the general view's own
     * tile (row 0). The tileview defaults to showing whatever tile sits at
     * grid (0,0), which is the general view again now that nothing else
     * occupies that slot - no explicit lv_tileview_set_tile() needed. */
    s_ip_tile = lv_tileview_add_tile(s_tileview, 0, 1, LV_DIR_TOP);
    lv_obj_set_style_bg_color(s_ip_tile, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ip_tile, LV_OPA_COVER, 0);
    build_ip_view(s_ip_tile);

    /* Page dots - a sibling of the tileview, not a child of any one tile,
     * so they stay fixed on screen while the tiles scroll underneath. */
    s_dots_row = lv_obj_create(scr);
    lv_obj_remove_style_all(s_dots_row);
    lv_obj_remove_flag(s_dots_row, LV_OBJ_FLAG_CLICKABLE); /* purely decorative - never
                                                             * steal a swipe from the
                                                             * tileview underneath */
    lv_obj_set_size(s_dots_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_dots_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dots_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_dots_row, 8, 0);
    lv_obj_align(s_dots_row, LV_ALIGN_BOTTOM_MID, 0, -28);
    for (int i = 0; i < s_page_count; i++) {
        lv_obj_t *dot = lv_obj_create(s_dots_row);
        lv_obj_remove_style_all(dot);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_size(dot, 6, 6);
        s_dots[i] = dot;
    }
    update_dots(0); /* general is always tile 0 */
    lv_obj_move_foreground(s_dots_row);

    /* Vertical dots - same recipe as the horizontal ones, on the left edge
     * instead of the bottom, showing position on the general/IP axis. */
    s_vdots_row = lv_obj_create(scr);
    lv_obj_remove_style_all(s_vdots_row);
    lv_obj_remove_flag(s_vdots_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_vdots_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_vdots_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_vdots_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_vdots_row, 8, 0);
    lv_obj_align(s_vdots_row, LV_ALIGN_LEFT_MID, 20, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *dot = lv_obj_create(s_vdots_row);
        lv_obj_remove_style_all(dot);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_size(dot, 6, 6);
        s_vdots[i] = dot;
    }
    update_vdots(0); /* starts on the general view */
    lv_obj_move_foreground(s_vdots_row);

    lv_obj_add_event_cb(s_tileview, tileview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    display_unlock();
}

void energy_rings_update(const energy_display_t *display)
{
    if (display == NULL || s_tileview == NULL) {
        return; /* energy_rings_show() hasn't run yet */
    }
    if (!display_lock(1000)) {
        return;
    }

    update_general_view(display);
    if (s_show_irradiance) {
        update_hero_view(&s_irradiance_view, &display->irradiance, "Ground irradiance");
    }
    if (s_show_solar) {
        update_hero_view(&s_solar_view, &display->solar, "Solar production");
    }
    if (s_show_grid) {
        update_hero_view(&s_grid_view, &display->grid, NULL);
    }
    if (s_show_battery) {
        update_hero_view(&s_battery_view, &display->battery, NULL);
    }
    if (s_show_consumption) {
        update_consumption_view(display);
    }

    display_unlock();
}
