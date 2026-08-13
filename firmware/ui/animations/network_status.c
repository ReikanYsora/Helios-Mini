#include "network_status.h"
#include "display.h"
#include "qr.h"
#include "figtree.h"
#include "lvgl.h"

#include <stdio.h>

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

    lv_obj_t *stack = lv_obj_create(scr);
    lv_obj_remove_style_all(stack);
    lv_obj_set_size(stack, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(stack, 8, 0);
    lv_obj_center(stack);

    lv_obj_t *title = lv_label_create(stack);
    lv_label_set_text(title, "Almost there");
    lv_obj_set_style_text_font(title, &figtree_24, 0);
    lv_obj_set_style_text_color(title, HELIOS_ACCENT_COLOR, 0);

    lv_obj_t *sub = lv_label_create(stack);
    lv_label_set_text(sub, "Scan to link Home Assistant");
    lv_obj_set_style_text_color(sub, lv_color_white(), 0);

    /* QR to the settings page's Home Assistant step, opened in a real browser
     * (a captive window can't do the app-switch a token needs). */
    char url[48];
    snprintf(url, sizeof(url), "http://%s/ha", ip);
    helios_qr_create(stack, url, 150);

    lv_obj_t *hint = lv_label_create(stack);
    lv_label_set_text_fmt(hint, "or open http://%s/", ip);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9a9aa5), 0);

    display_unlock();
}
