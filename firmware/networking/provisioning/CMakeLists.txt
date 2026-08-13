idf_component_register(
    SRCS "provisioning.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config storage config display qr fonts http_forms driver esp_wifi esp_netif esp_event esp_http_server lwip
    PRIV_REQUIRES esp_timer
)
