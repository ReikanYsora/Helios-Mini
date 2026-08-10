#include "wifi_sta.h"
#include "storage.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_sta";
static const char *NVS_NAMESPACE = "helios_mini";

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0

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
        ESP_LOGI(TAG, "connected, ip=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void load_credentials(wifi_config_t *cfg)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    bool have_ssid = storage_get_string(NVS_NAMESPACE, "wifi_ssid", ssid, sizeof(ssid)) == ESP_OK;

    if (!have_ssid) {
        ESP_LOGW(TAG, "no stored Wi-Fi credentials (provisioning is V0.2 scope); "
                      "falling back to CONFIG_HELIOS_WIFI_DEV_SSID for bring-up");
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
