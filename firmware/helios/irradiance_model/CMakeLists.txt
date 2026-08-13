idf_component_register(
    SRCS "irradiance_model.c" "sun_math.c"
    INCLUDE_DIRS "include"
    REQUIRES config energy_math ha_client mbedtls json esp_timer
)
