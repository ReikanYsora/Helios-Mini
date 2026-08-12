idf_component_register(
    SRCS "diagnostics.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer display audio esp_wifi
)
