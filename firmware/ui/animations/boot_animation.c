/* LVGL v9 API surface used here (lv_image_*, LV_IMAGE_DECLARE) has not been
 * compiled against yet in this repo - verify exact symbol names against the
 * resolved lvgl/lvgl component version on first build. See
 * docs/HARDWARE_REFERENCE.md. */
#include "boot_animation.h"
#include "lvgl.h"

LV_IMAGE_DECLARE(helios_logo);

/* Helios amber, matched to assets/brand/helios-logo.svg (#efb428). */
#define HELIOS_ACCENT_COLOR lv_color_hex(0xefb428)

#define BOOT_DOT_GROW_MS   300
#define BOOT_ARC_SWEEP_MS  900
#define BOOT_ARC_DELAY_MS  300
#define BOOT_LOGO_FADE_MS  400

typedef struct {
    lv_obj_t *dot;
    lv_obj_t *arc;
    lv_obj_t *logo;
} boot_scene_t;

static boot_scene_t s_scene;

static void dot_grow_cb(void *obj, int32_t v)
{
    lv_obj_set_size((lv_obj_t *)obj, v, v);
    lv_obj_center((lv_obj_t *)obj);
}

static void arc_sweep_cb(void *obj, int32_t v)
{
    lv_arc_set_angles((lv_obj_t *)obj, 270, 270 + v);
}

static void logo_fade_cb(void *obj, int32_t v)
{
    lv_obj_set_style_image_opa((lv_obj_t *)obj, v, 0);
}

static void arc_sweep_done_cb(lv_anim_t *anim)
{
    boot_scene_t *scene = (boot_scene_t *)lv_anim_get_user_data(anim);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, scene->logo);
    lv_anim_set_exec_cb(&fade, logo_fade_cb);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&fade, BOOT_LOGO_FADE_MS);
    lv_anim_start(&fade);
}

void boot_animation_start(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Step 2: small central light. */
    s_scene.dot = lv_obj_create(scr);
    lv_obj_remove_style_all(s_scene.dot);
    lv_obj_set_style_radius(s_scene.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_scene.dot, HELIOS_ACCENT_COLOR, 0);
    lv_obj_set_style_bg_opa(s_scene.dot, LV_OPA_COVER, 0);
    lv_obj_set_size(s_scene.dot, 4, 4);
    lv_obj_center(s_scene.dot);

    /* Step 3: circular animation. */
    s_scene.arc = lv_arc_create(scr);
    lv_obj_remove_style(s_scene.arc, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_scene.arc, 180, 180);
    lv_obj_center(s_scene.arc);
    lv_arc_set_bg_angles(s_scene.arc, 0, 360);
    lv_arc_set_angles(s_scene.arc, 270, 270);
    lv_obj_set_style_arc_opa(s_scene.arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_scene.arc, HELIOS_ACCENT_COLOR, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_scene.arc, 4, LV_PART_INDICATOR);

    /* Step 4-5: Helios logo appears and fades in once the sweep completes. */
    s_scene.logo = lv_image_create(scr);
    lv_image_set_src(s_scene.logo, &helios_logo);
    lv_obj_center(s_scene.logo);
    lv_obj_set_style_image_opa(s_scene.logo, LV_OPA_TRANSP, 0);

    lv_anim_t grow;
    lv_anim_init(&grow);
    lv_anim_set_var(&grow, s_scene.dot);
    lv_anim_set_exec_cb(&grow, dot_grow_cb);
    lv_anim_set_values(&grow, 4, 12);
    lv_anim_set_time(&grow, BOOT_DOT_GROW_MS);
    lv_anim_start(&grow);

    lv_anim_t sweep;
    lv_anim_init(&sweep);
    lv_anim_set_var(&sweep, s_scene.arc);
    lv_anim_set_exec_cb(&sweep, arc_sweep_cb);
    lv_anim_set_values(&sweep, 0, 360);
    lv_anim_set_time(&sweep, BOOT_ARC_SWEEP_MS);
    lv_anim_set_delay(&sweep, BOOT_ARC_DELAY_MS);
    lv_anim_set_user_data(&sweep, &s_scene);
    lv_anim_set_completed_cb(&sweep, arc_sweep_done_cb);
    lv_anim_start(&sweep);

    /* Total: ~300ms dot + 300ms delay + 900ms sweep + 400ms fade =~ 1.9s,
     * comfortably under the <3s target (spec Section 15) excluding network
     * time, leaving headroom for the Wi-Fi/HA status steps in V0.2. */
}
