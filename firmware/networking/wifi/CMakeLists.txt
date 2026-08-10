idf_component_register(
    SRCS "wifi_sta.c"
    INCLUDE_DIRS "include"
    REQUIRES storage esp_wifi esp_netif esp_event nvs_flash
)
