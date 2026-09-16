#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "gas_sensor.h"
#include "gas_network.h"
#include "gas_discord.h"

static const char *TAG = "GAS_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "  Gas Air Quality Monitor");
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "Threshold : %d mV", CONFIG_GAS_THRESHOLD_MV);
    ESP_LOGI(TAG, "Period    : %d ms", CONFIG_GAS_CHECK_PERIOD_MS);
    ESP_LOGI(TAG, "Cooldown  : %d s", CONFIG_GAS_ALARM_COOLDOWN_S);
    ESP_LOGI(TAG, "Discord   : %s",
             strlen(CONFIG_GAS_DISCORD_WEBHOOK) > 0 ? "configured" : "NOT SET");
    ESP_LOGI(TAG, "Device    : %s", CONFIG_GAS_DEVICE_NAME);
    ESP_LOGI(TAG, "=====================================");

    ESP_ERROR_CHECK(gas_sensor_init());
    ESP_ERROR_CHECK(gas_network_start());

    vTaskDelay(pdMS_TO_TICKS(3000));

    while (!gas_network_is_connected()) {
        ESP_LOGW(TAG, "Waiting for Wi-Fi connection...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "Connected to Wi-Fi, IP = %s", gas_network_get_ip());

    esp_err_t derr = gas_discord_init();
    if (derr != ESP_OK) {
        ESP_LOGE(TAG, "Discord init failed: %s (alarm disabled)",
                 esp_err_to_name(derr));
    }

    TickType_t last_alarm_tick = 0;

    while (1) {
        uint16_t mv = gas_sensor_read_mv();
        bool alarm = gas_sensor_is_alarm(mv);

        if (alarm) {
            ESP_LOGW(TAG, "!!! GAS ALARM !!! reading = %u mV", mv);

            TickType_t now = xTaskGetTickCount();
            TickType_t cooldown =
                pdMS_TO_TICKS(CONFIG_GAS_ALARM_COOLDOWN_S * 1000);

            if ((now - last_alarm_tick) >= cooldown) {
                if (derr == ESP_OK) {
                    bool ok = gas_discord_send_alarm(mv);
                    if (ok) {
                        ESP_LOGI(TAG, "Discord alarm sent");
                    } else {
                        ESP_LOGE(TAG, "Discord alarm FAILED");
                    }
                }
                last_alarm_tick = now;
            } else {
                ESP_LOGD(TAG, "Alarm in cooldown, skipping Discord send");
            }
        } else {
            ESP_LOGI(TAG, "OK: %u mV (threshold %d mV)",
                     mv, CONFIG_GAS_THRESHOLD_MV);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_GAS_CHECK_PERIOD_MS));
    }
}
