#include "i2c_bus.h"
#include "board_config.h"
#include "esp_err.h"

static i2c_master_bus_handle_t s_bus = NULL;

void i2c_bus_init(void)
{
    if (s_bus != NULL) {
        return;
    }

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = HELIOS_I2C_PORT,
        .scl_io_num = HELIOS_PIN_I2C_SCL,
        .sda_io_num = HELIOS_PIN_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_bus));
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus;
}
