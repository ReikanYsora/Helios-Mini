#include "network_status.h"
#include "display.h"
#include "lvgl.h"

/* Helios amber, matched to assets/brand/helios-logo.svg (#efb428). */
#define HELIOS_ACCENT_COLOR lv_color_hex(0xefb428)

void network_status_show_connected(const char *ip)
{
    if (!display_lock(1000)) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr); /* drop whatever was on screen (boot animation, setup notice) */
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Helios Mini");
    lv_obj_set_style_text_color(title, HELIOS_ACCENT_COLOR, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *ip_label = lv_label_create(scr);
    lv_label_set_text_fmt(ip_label, "http://%s/", ip);
    lv_obj_set_style_text_color(ip_label, lv_color_white(), 0);
    lv_obj_align(ip_label, LV_ALIGN_CENTER, 0, 15);

    display_unlock();
}
