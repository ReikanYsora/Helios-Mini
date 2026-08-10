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
        audio
        wifi
        provisioning
        storage
        diagnostics
        animations
)
