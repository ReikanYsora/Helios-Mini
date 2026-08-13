idf_component_register(
    SRCS "settings_server.c"
    INCLUDE_DIRS "include"
    REQUIRES storage config http_forms ha_client ha_discovery diagnostics wifi esp_http_server energy_model ha_ws mqtt_bridge display
)
