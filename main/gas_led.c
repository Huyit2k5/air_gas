#include "gas_led.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

esp_err_t gas_led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_GAS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(CONFIG_GAS_LED_GPIO, 0);
    return ESP_OK;
}

void gas_led_set(bool on)
{
    gpio_set_level(CONFIG_GAS_LED_GPIO, on ? 1 : 0);
}
