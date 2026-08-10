idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config touch driver esp_lcd lvgl
    PRIV_REQUIRES esp_timer
)
