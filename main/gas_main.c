#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "gas_sensor.h"
#include "gas_network.h"
#include "gas_discord.h"
#include "gas_led.h"
#include "gas_mqtt.h"

static const char *TAG = "GAS_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "  Gas Air Quality Monitor");
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "Threshold : %d ppm (est. LPG)", CONFIG_GAS_THRESHOLD_PPM);
    ESP_LOGI(TAG, "Period    : %d ms", CONFIG_GAS_CHECK_PERIOD_MS);
    ESP_LOGI(TAG, "Cooldown  : %d s", CONFIG_GAS_ALARM_COOLDOWN_S);
    ESP_LOGI(TAG, "Discord   : %s",
             strlen(CONFIG_GAS_DISCORD_WEBHOOK) > 0 ? "configured" : "NOT SET");
    ESP_LOGI(TAG, "Device    : %s", CONFIG_GAS_DEVICE_NAME);
    ESP_LOGI(TAG, "=====================================");

    ESP_ERROR_CHECK(gas_sensor_init());
    ESP_ERROR_CHECK(gas_led_init());
    ESP_ERROR_CHECK(gas_network_start());

    esp_err_t derr = gas_discord_init();
    if (derr != ESP_OK) {
        ESP_LOGE(TAG, "Discord init failed: %s (Discord alerts disabled, "
                      "local LED alarm still active)",
                 esp_err_to_name(derr));
    }

    esp_err_t merr = gas_mqtt_init();
    if (merr != ESP_OK) {
        ESP_LOGW(TAG, "MQTT init failed: %s (MQTT publishing disabled)",
                 esp_err_to_name(merr));
    }

    TickType_t last_alarm_tick = 0;
    bool was_connected = false;

    /* Sensor monitoring + local LED alarm run immediately and do not wait
     * for Wi-Fi: a gas leak must be signalled even if the network is down.
     * Discord notifications are best-effort and only sent while connected. */
    while (1) {
        bool connected = gas_network_is_connected();
        if (connected && !was_connected) {
            ESP_LOGI(TAG, "Connected to Wi-Fi, IP = %s", gas_network_get_ip());
        }
        was_connected = connected;

        gas_reading_t reading = gas_sensor_read();
        bool alarm = gas_sensor_is_alarm(reading.ppm);

        gas_led_set(alarm);
        gas_mqtt_publish_reading(reading.ppm, reading.mv, alarm);

        if (alarm) {
            ESP_LOGW(TAG, "!!! GAS ALARM !!! ~%d ppm (raw %u mV)",
                     (int)reading.ppm, reading.mv);

            TickType_t now = xTaskGetTickCount();
            TickType_t cooldown =
                pdMS_TO_TICKS(CONFIG_GAS_ALARM_COOLDOWN_S * 1000);

            if ((now - last_alarm_tick) >= cooldown) {
                if (derr == ESP_OK && connected) {
                    bool ok = gas_discord_send_alarm(reading.ppm, reading.mv);
                    if (ok) {
                        ESP_LOGI(TAG, "Discord alarm sent");
                    } else {
                        ESP_LOGE(TAG, "Discord alarm FAILED");
                    }
                } else if (derr == ESP_OK && !connected) {
                    ESP_LOGW(TAG, "No Wi-Fi, skipping Discord send "
                                  "(LED alarm still active)");
                }
                gas_mqtt_publish_alarm(reading.ppm, reading.mv);
                last_alarm_tick = now;
            } else {
                ESP_LOGD(TAG, "Alarm in cooldown, skipping Discord send");
            }
        } else {
            ESP_LOGI(TAG, "OK: ~%d ppm (raw %u mV, threshold %d ppm)",
                     (int)reading.ppm, reading.mv, CONFIG_GAS_THRESHOLD_PPM);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_GAS_CHECK_PERIOD_MS));
    }
}
