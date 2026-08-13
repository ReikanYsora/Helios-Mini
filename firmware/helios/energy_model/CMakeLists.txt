idf_component_register(
    SRCS "energy_config.c" "energy_model.c"
    INCLUDE_DIRS "include"
    REQUIRES storage ha_ws home
)
