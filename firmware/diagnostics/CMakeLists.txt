idf_component_register(
    SRCS "diagnostics.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_wifi esp_driver_tsens
)
