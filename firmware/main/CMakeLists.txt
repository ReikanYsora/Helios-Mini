idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        board_config
        power
        i2c_bus
        touch
        display
        buttons
        wifi
        provisioning
        settings_server
        energy_model
        irradiance_model
        mqtt_bridge
        home
        storage
        diagnostics
        animations
)
