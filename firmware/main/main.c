#include "power.h"
#include "storage.h"
#include "i2c_bus.h"
#include "display.h"
#include "buttons.h"
#include "wifi_sta.h"
#include "provisioning.h"
#include "settings_server.h"
#include "energy_model.h"
#include "irradiance_model.h"
#include "energy_rings.h"
#include "mqtt_bridge.h"
#include "diagnostics.h"
#include "boot_animation.h"
#include "network_status.h"
#include "esp_log.h"

static const char *TAG = "helios_mini";

/* wifi_sta_connected_cb_t has one slot and no user-data parameter, so this
 * one wrapper does everything that needs to happen on every IP change:
 * always feed ui/home's swipe-up IP screen, and only ALSO show the
 * one-time IP notice while Home Assistant isn't configured yet (see the
 * comment at the registration site below). */
static bool s_show_ip_notice = false;

static void on_wifi_got_ip(const char *ip)
{
    energy_rings_set_ip(ip);
    if (s_show_ip_notice) {
        network_status_show_connected(ip);
    }
}

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
        s_show_ip_notice = !ha_already_configured;
        wifi_sta_set_connected_cb(on_wifi_got_ip);
        wifi_sta_start();
        settings_server_start();
        /* Polls Home Assistant for the energy rings and switches the screen
         * from whatever's currently up (the boot logo, or the IP notice) to
         * them once the Energy Dashboard has a source configured - see
         * docs/HARDWARE_REFERENCE.md. Only makes sense once the device can
         * actually reach Home Assistant, so it's not started in the
         * setup-portal branch above. */
        energy_model_start();
        /* Location (from HA's own /api/config) + Open-Meteo cloud cover for
         * the irradiance ring/page - independent of the Energy Dashboard,
         * so it's started unconditionally alongside it rather than gated
         * on ha_already_configured/energy_model_start() above. */
        irradiance_model_start();
        /* MQTT (if configured on /mqtt) - device controls/diagnostics as
         * HA-discovered entities. Also independent of the Energy
         * Dashboard. */
        mqtt_bridge_start();
    }

    diagnostics_start();

    ESP_LOGI(TAG, "Helios Mini V0.1 hardware bring-up started");
}
