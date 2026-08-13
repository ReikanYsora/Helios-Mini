idf_component_register(
    SRCS "ha_client.c"
    INCLUDE_DIRS "include"
    REQUIRES energy_math esp_http_client json
)
