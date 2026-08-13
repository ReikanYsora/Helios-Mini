idf_component_register(
    SRCS "mqtt_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES storage config display diagnostics irradiance_model mqtt json esp_app_format
)
