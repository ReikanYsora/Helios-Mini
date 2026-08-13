idf_component_register(
    SRCS "irradiance_model.c" "sun_math.c"
    INCLUDE_DIRS "include"
    REQUIRES storage ha_client mbedtls json
)
