#include "power.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_err.h"

void power_latch_on(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << HELIOS_PIN_POWER_LATCH,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(HELIOS_PIN_POWER_LATCH, 1);
}

void power_latch_off(void)
{
    gpio_set_level(HELIOS_PIN_POWER_LATCH, 0);
}
