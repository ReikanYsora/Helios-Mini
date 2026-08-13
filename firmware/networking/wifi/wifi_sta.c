#include "wifi_sta.h"
#include "storage.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_sta";
static const char *NVS_NAMESPACE = "helios_mini";

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0

static wifi_sta_connected_cb_t s_connected_cb = NULL;
static bool s_sntp_started = false; /* helios/irradiance_model needs a real UTC clock for its sun-position math */

void wifi_sta_set_connected_cb(wifi_sta_connected_cb_t cb)
{
    s_connected_cb = cb;
}

static void start_sntp_once(void)
{
    if (s_sntp_started) {
        return;
    }
    s_sntp_started = true;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config); /* async - syncs in the background, no blocking wait here */
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "disconnected, retrying");
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "connected, ip=%s", ip_str);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        start_sntp_once();
        if (s_connected_cb) {
            s_connected_cb(ip_str);
        }
    }
}

static void load_credentials(wifi_config_t *cfg)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    bool have_ssid = storage_get_string(NVS_NAMESPACE, "wifi_ssid", ssid, sizeof(ssid)) == ESP_OK;

    if (!have_ssid) {
        /* main.c only calls wifi_sta_start() once provisioning_has_credentials()
         * is true, so this is a defensive fallback, not the normal path. */
        ESP_LOGW(TAG, "no stored Wi-Fi credentials; falling back to CONFIG_HELIOS_WIFI_DEV_SSID");
        strncpy(ssid, CONFIG_HELIOS_WIFI_DEV_SSID, sizeof(ssid) - 1);
        strncpy(pass, CONFIG_HELIOS_WIFI_DEV_PASSWORD, sizeof(pass) - 1);
    } else {
        storage_get_string(NVS_NAMESPACE, "wifi_pass", pass, sizeof(pass));
    }

    strncpy((char *)cfg->sta.ssid, ssid, sizeof(cfg->sta.ssid) - 1);
    strncpy((char *)cfg->sta.password, pass, sizeof(cfg->sta.password) - 1);
}

void wifi_sta_start(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {0};
    load_credentials(&wifi_config);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool wifi_sta_wait_connected(int timeout_ms)
{
    if (timeout_ms == 0) {
        return (xEventGroupGetBits(s_wifi_events) & WIFI_CONNECTED_BIT) != 0;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_sta_get_ip(char *out, size_t out_len)
{
    if (out != NULL && out_len > 0) {
        out[0] = '\0';
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        return false;
    }
    if (out != NULL) {
        snprintf(out, out_len, IPSTR, IP2STR(&ip_info.ip));
    }
    return true;
}

void wifi_sta_get_setup_ap_ssid(char *out, size_t out_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_size, "HELIOS-MINI-%02X%02X", mac[4], mac[5]);
}

static bool s_ap_enabled = false;
static bool s_ap_netif_created = false;

bool wifi_sta_is_setup_ap_enabled(void)
{
    return s_ap_enabled;
}

bool wifi_sta_set_setup_ap_enabled(bool enable)
{
    if (enable == s_ap_enabled) {
        return true;
    }

    if (enable) {
        if (!s_ap_netif_created) {
            esp_netif_create_default_wifi_ap();
            s_ap_netif_created = true;
        }

        char ap_ssid[24];
        wifi_sta_get_setup_ap_ssid(ap_ssid, sizeof(ap_ssid));

        wifi_config_t ap_config = {0};
        strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
        ap_config.ap.ssid_len = strlen(ap_ssid);
        ap_config.ap.channel = 1;
        ap_config.ap.authmode = WIFI_AUTH_OPEN; /* same tradeoff as networking/provisioning's AP */
        ap_config.ap.max_connection = 4;

        if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK ||
            esp_wifi_set_config(WIFI_IF_AP, &ap_config) != ESP_OK) {
            ESP_LOGE(TAG, "failed to enable setup AP");
            return false;
        }
        ESP_LOGI(TAG, "setup AP '%s' enabled at http://192.168.4.1/ alongside the station connection", ap_ssid);
    } else {
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            ESP_LOGE(TAG, "failed to disable setup AP");
            return false;
        }
        ESP_LOGI(TAG, "setup AP disabled");
    }

    s_ap_enabled = enable;
    return true;
}
