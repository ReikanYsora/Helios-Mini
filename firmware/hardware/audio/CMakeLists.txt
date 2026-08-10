idf_component_register(
    SRCS "audio.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config i2c_bus driver
)
