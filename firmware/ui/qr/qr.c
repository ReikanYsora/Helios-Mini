#include "qr.h"

#include <string.h>

lv_obj_t *helios_qr_create(lv_obj_t *parent, const char *data, int size)
{
    lv_obj_t *qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, data, (uint32_t)strlen(data));

    /* White quiet-zone border so a scanner locks on against the black UI. */
    lv_obj_set_style_bg_color(qr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(qr, 10, 0);
    lv_obj_set_style_radius(qr, 4, 0);
    return qr;
}
