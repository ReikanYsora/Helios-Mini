#pragma once

#include "driver/i2c_master.h"

/* Initializes the I2C bus shared by the touch controller (CST820) and the
 * audio codec (ES8311). Idempotent - safe to call once from app_main()
 * before any component that needs the bus. */
void i2c_bus_init(void);

/* Returns the shared bus handle. i2c_bus_init() must be called first. */
i2c_master_bus_handle_t i2c_bus_get_handle(void);
