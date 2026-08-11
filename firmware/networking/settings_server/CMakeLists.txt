idf_component_register(
    SRCS "settings_server.c"
    INCLUDE_DIRS "include"
    REQUIRES storage http_forms esp_http_server
)
