#include "power.h"
#include "storage.h"
#include "i2c_bus.h"
#include "display.h"
#include "buttons.h"
#include "audio.h"
#include "wifi_sta.h"
#include "provisioning.h"
#include "settings_server.h"
#include "energy_model.h"
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
    }
    /* Automatic startup chime disabled for now - still being tuned (see
     * docs/HARDWARE_REFERENCE.md) and nobody wants it firing on every boot
     * while that's in progress. audio_play_startup_tone() still works and
     * is reachable manually from the settings server's /debug page. */

    if (force_provisioning || !provisioning_has_credentials()) {
        ESP_LOGI(TAG, "no Wi-Fi credentials (or BOOT held at power-on): starting setup portal");
        provisioning_start_portal();
    } else {
        /* The IP notice is only useful as a "how do I reach the settings
         * app" pointer for whoever hasn't set up Home Assistant yet -
         * once it's configured, the device is heading for the energy
         * rings anyway (helios/energy_model, below), so showing the IP
         * first is just a flash of text before it gets replaced. Skip
         * it in that case and leave the boot logo up until the rings are
         * ready instead. */
        char ha_url[128] = {0};
        char ha_token[256] = {0};
        bool ha_already_configured = settings_get_ha_config(ha_url, sizeof(ha_url), ha_token, sizeof(ha_token));
        if (!ha_already_configured) {
            wifi_sta_set_connected_cb(network_status_show_connected);
        }
        wifi_sta_start();
        settings_server_start();
        /* Polls Home Assistant for the energy rings and switches the screen
         * from whatever's currently up (the boot logo, or the IP notice) to
         * them once the Energy Dashboard has a source configured - see
         * docs/HARDWARE_REFERENCE.md. Only makes sense once the device can
         * actually reach Home Assistant, so it's not started in the
         * setup-portal branch above. */
        energy_model_start();
    }

    diagnostics_start();

    ESP_LOGI(TAG, "Helios Mini V0.1 hardware bring-up started");
}
