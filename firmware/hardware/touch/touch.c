#include "touch.h"
#include "board_config.h"
#include "i2c_bus.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

/* CST820 register protocol, ported from Waveshare's own reference firmware
 * for this board: gesture/event byte pair at 0x02, X/Y position at 0x03.
 * See docs/HARDWARE_REFERENCE.md. */
#define CST820_REG_GESTURE  0x02
#define CST820_REG_POSITION 0x03

static i2c_master_dev_handle_t s_touch_dev = NULL;

static void touch_reset(void)
{
    gpio_set_level(HELIOS_PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(HELIOS_PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(HELIOS_PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

void touch_init(void)
{
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << HELIOS_PIN_TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));

    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << HELIOS_PIN_TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&int_cfg));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HELIOS_TOUCH_I2C_ADDR,
        .scl_speed_hz = 300000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_get_handle(), &dev_cfg, &s_touch_dev));

    touch_reset();
}

bool touch_read(uint16_t *x, uint16_t *y)
{
    uint8_t reg;
    uint8_t gesture[2] = {0};
    uint8_t pos[4] = {0};

    reg = CST820_REG_GESTURE;
    if (i2c_master_transmit_receive(s_touch_dev, &reg, 1, gesture, sizeof(gesture), pdMS_TO_TICKS(50)) != ESP_OK) {
        return false;
    }

    uint8_t event = gesture[1] >> 6;
    if (gesture[0] == 0 || event == 0x01) {
        /* No point count, or a "lift" event with no coordinates to trust. */
        return false;
    }

    reg = CST820_REG_POSITION;
    if (i2c_master_transmit_receive(s_touch_dev, &reg, 1, pos, sizeof(pos), pdMS_TO_TICKS(50)) != ESP_OK) {
        return false;
    }

    *x = ((uint16_t)(pos[0] & 0x0F) << 8) | pos[1];
    *y = ((uint16_t)(pos[2] & 0x0F) << 8) | pos[3];
    return true;
}
