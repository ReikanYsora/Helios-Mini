#include "power.h"
#include "storage.h"
#include "i2c_bus.h"
#include "display.h"
#include "buttons.h"
#include "audio.h"
#include "wifi_sta.h"
#include "provisioning.h"
#include "settings_server.h"
#include "diagnostics.h"
#include "boot_animation.h"
#include "network_status.h"
#include "esp_log.h"

static const char *TAG = "helios_mini";

static void on_button_event(helios_button_id_t id, helios_button_event_t event, void *ctx)
{
    const char *name = (id == HELIOS_BUTTON_BOOT) ? "BOOT" : "PWR";
    const char *kind = (event == HELIOS_BUTTON_EVENT_SINGLE_CLICK) ? "single-click"
                      : (event == HELIOS_BUTTON_EVENT_DOUBLE_CLICK) ? "double-click"
                      : "long-press";
    ESP_LOGI(TAG, "%s button: %s", name, kind);

    if (id == HELIOS_BUTTON_PWR && event == HELIOS_BUTTON_EVENT_LONG_PRESS) {
        ESP_LOGW(TAG, "PWR long-press: releasing power latch");
        power_latch_off();
    }
}

void app_main(void)
{
    /* Must happen before anything else: the board's power rail depends on
     * this being asserted quickly after the PWR-button press that woke it
     * up, or it powers back off. See docs/HARDWARE_REFERENCE.md. */
    power_latch_on();

    /* Checked this early so the window to catch "user is holding BOOT at
     * power-on" isn't eaten by the init steps below. */
    bool force_provisioning = provisioning_boot_forced();

    ESP_ERROR_CHECK(storage_init());

    i2c_bus_init();
    display_init(); /* also brings up touch internally */
    boot_animation_start();

    buttons_init(on_button_event, NULL);

    if (!audio_init()) {
        ESP_LOGW(TAG, "audio codec bring-up failed; I2S bus and PA enable are still usable");
    } else {
        /* Confirms the speaker path works without needing an audio asset -
         * see docs/SPEC.md Section 5 (audio isn't a primary V1 feature, but
         * it should be validated and ready for notification sounds). */
        audio_play_startup_tone();
    }

    if (force_provisioning || !provisioning_has_credentials()) {
        ESP_LOGI(TAG, "no Wi-Fi credentials (or BOOT held at power-on): starting setup portal");
        provisioning_start_portal();
    } else {
        /* Keeps the on-screen IP current across (re)connects, so the
         * settings server below is always reachable at a glance. */
        wifi_sta_set_connected_cb(network_status_show_connected);
        wifi_sta_start();
        settings_server_start();
    }

    diagnostics_start();

    ESP_LOGI(TAG, "Helios Mini V0.1 hardware bring-up started");
}
