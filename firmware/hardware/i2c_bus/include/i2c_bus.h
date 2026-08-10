#pragma once

#include "driver/i2c.h"

/* Initializes the I2C bus shared by the touch controller (CST820) and the
 * audio codec (ES8311), using the legacy driver/i2c.h API (see
 * board_config.h for why). Idempotent - safe to call once from app_main()
 * before any component that needs the bus. */
void i2c_bus_init(void);

/* Returns the shared bus's port number. i2c_bus_init() must be called
 * first. */
i2c_port_t i2c_bus_get_port(void);
