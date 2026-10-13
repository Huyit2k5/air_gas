#include "gas_buzzer.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_GAS_BUZZER_ENABLE

#include "driver/gpio.h"

static const char *TAG = "GAS_BUZZER";

static volatile gas_buzzer_mode_t s_mode = GAS_BUZZER_OFF;

static void buzzer_level(int level)
{
    gpio_set_level(CONFIG_GAS_BUZZER_GPIO, level);
}

/* Runs on its own task so the beep rhythm is independent of the (slow)
 * sensor read period and keeps going while the main loop is blocked. */
static void buzzer_task(void *arg)
{
    (void)arg;
    while (1) {
        switch (s_mode) {
        case GAS_BUZZER_GAS:
            buzzer_level(1);
            vTaskDelay(pdMS_TO_TICKS(150));
            buzzer_level(0);
            vTaskDelay(pdMS_TO_TICKS(150));
            break;
        case GAS_BUZZER_POOR_AIR:
            buzzer_level(1);
            vTaskDelay(pdMS_TO_TICKS(200));
            buzzer_level(0);
            vTaskDelay(pdMS_TO_TICKS(1800));
            break;
        default:
            buzzer_level(0);
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }
    }
}

esp_err_t gas_buzzer_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_GAS_BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

#if CONFIG_GAS_BUZZER_SELFTEST
    ESP_LOGW(TAG, "SELFTEST on GPIO %d: 2 s ON / 2 s OFF forever "
                  "(sensors, Wi-Fi and MQTT are NOT running)",
             CONFIG_GAS_BUZZER_GPIO);
    while (1) {
        buzzer_level(1);
        ESP_LOGW(TAG, "SELFTEST: GPIO %d HIGH (buzzer should sound)",
                 CONFIG_GAS_BUZZER_GPIO);
        vTaskDelay(pdMS_TO_TICKS(2000));
        buzzer_level(0);
        ESP_LOGW(TAG, "SELFTEST: GPIO %d LOW (buzzer should be silent)",
                 CONFIG_GAS_BUZZER_GPIO);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#endif

    if (xTaskCreate(buzzer_task, "buzzer", 2048, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Active buzzer ready on GPIO %d", CONFIG_GAS_BUZZER_GPIO);
    return ESP_OK;
}

void gas_buzzer_set(gas_buzzer_mode_t mode)
{
    s_mode = mode;
}

#else /* !CONFIG_GAS_BUZZER_ENABLE */

esp_err_t gas_buzzer_init(void)
{
    ESP_LOGI("GAS_BUZZER", "Buzzer disabled (CONFIG_GAS_BUZZER_ENABLE=n)");
    return ESP_OK;
}

void gas_buzzer_set(gas_buzzer_mode_t mode)
{
    (void)mode;
}

#endif /* CONFIG_GAS_BUZZER_ENABLE */
