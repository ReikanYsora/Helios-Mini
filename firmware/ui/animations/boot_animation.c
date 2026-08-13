#include "boot_animation.h"
#include "display.h"
#include "lvgl.h"
#include "assets/helios_logo_pieces.h"

#include <stdlib.h>

/* The Helios logo draws itself in: its 12 sun-flame rays light up one by
 * one clockwise around the circle, then the central disc, instead of a
 * generic spinner - see docs/HARDWARE_REFERENCE.md. Each piece is its own
 * small LVGL image (tools/asset-gen/svg_pieces_to_lvgl.py, cropped to its
 * own bounding box - most of a single flame's canvas is transparent, so
 * this is smaller in flash than one full-canvas image would be),
 * positioned via the offsets baked into helios_logo_pieces[] at
 * generation time. */
#define BOOT_PIECE_STAGGER_MS 90
#define BOOT_PIECE_FADE_MS    400

/* helios_logo_pieces[] is in SVG source order (starting near 10 o'clock,
 * sweeping counter-clockwise) - reveal_order instead starts at the
 * topmost ray (index 10, ~12 o'clock) and sweeps clockwise from there
 * (see docs/HARDWARE_REFERENCE.md for how the angles were worked out),
 * disc (index 12) always last. */
static const uint8_t s_reveal_order[HELIOS_LOGO_PIECE_COUNT] = {
    10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 11, 12,
};

static lv_obj_t *s_pieces[HELIOS_LOGO_PIECE_COUNT];

static void piece_fade_cb(void *obj, int32_t v)
{
    lv_obj_set_style_image_opa((lv_obj_t *)obj, v, 0);
}

void boot_animation_start(void)
{
    /* Mandatory: this runs on the "main" task, while the display component's
     * own "lvgl" task is concurrently looping lv_timer_handler() in the
     * background. LVGL is not thread-safe - without this lock, the two
     * tasks mutate the object tree/invalidated-area bookkeeping at the same
     * time, which corrupts it and hangs (observed as a task watchdog
     * timeout inside lv_inv_area on real hardware). See display.h. */
    if (!display_lock(0)) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    for (int pos = 0; pos < HELIOS_LOGO_PIECE_COUNT; pos++) {
        int piece_idx = s_reveal_order[pos];
        const helios_logo_piece_t *piece = &helios_logo_pieces[piece_idx];

        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, piece->img);
        lv_obj_align(img, LV_ALIGN_CENTER, piece->center_dx, piece->center_dy);
        lv_obj_set_style_image_opa(img, LV_OPA_TRANSP, 0);
        s_pieces[piece_idx] = img;

        lv_anim_t fade;
        lv_anim_init(&fade);
        lv_anim_set_var(&fade, img);
        lv_anim_set_exec_cb(&fade, piece_fade_cb);
        lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&fade, BOOT_PIECE_FADE_MS);
        lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
        lv_anim_set_delay(&fade, (uint32_t)pos * BOOT_PIECE_STAGGER_MS);
        lv_anim_start(&fade);
    }

    /* Total: 12 rays at 90ms apart + the disc, each fading in over 400ms
     * = ~(12 * 90) + 400 =~ 1480ms, comfortably under the <3s target (spec
     * Section 15) excluding network time. */

    display_unlock();
}
