#include "i2c_bus.h"
#include "board_config.h"
#include "esp_err.h"

static bool s_initialized = false;

void i2c_bus_init(void)
{
    if (s_initialized) {
        return;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = HELIOS_PIN_I2C_SDA,
        .scl_io_num = HELIOS_PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 300000,
    };
    ESP_ERROR_CHECK(i2c_param_config(HELIOS_I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(HELIOS_I2C_PORT, conf.mode, 0, 0, 0));
    s_initialized = true;
}

i2c_port_t i2c_bus_get_port(void)
{
    return HELIOS_I2C_PORT;
}
