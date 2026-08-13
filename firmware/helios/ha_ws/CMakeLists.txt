idf_component_register(
    SRCS "ha_ws.c"
    INCLUDE_DIRS "include"
    REQUIRES config energy_math ha_client json esp_websocket_client esp_timer esp_netif
)
