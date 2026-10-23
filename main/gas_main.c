#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "gas_config.h"
#include "gas_control.h"
#include "gas_sensor.h"
#include "air_sensor.h"
#include "gas_network.h"
#include "gas_discord.h"
#include "gas_led.h"
#include "gas_buzzer.h"
#include "gas_fan.h"
#include "gas_valve.h"
#include "gas_mqtt.h"

static const char *TAG = "GAS_MAIN";

#if !CONFIG_GAS_FAN_ENABLE
#define CONFIG_GAS_FAN_RUNON_S 0
#define CONFIG_GAS_FAN_POOR_PERCENT 0
#endif

/* Values reported while a simulation command is active (see gas_control.h). */
#define SIM_GAS_MV 1950
#define SIM_AIR_MV 800

void app_main(void)
{
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "  Gas Air Quality Monitor %s", GAS_FW_VERSION);
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "Threshold : %d ppm (est. LPG)", CONFIG_GAS_THRESHOLD_PPM);
    ESP_LOGI(TAG, "Period    : %d ms", CONFIG_GAS_CHECK_PERIOD_MS);
    ESP_LOGI(TAG, "Cooldown  : %d s", CONFIG_GAS_ALARM_COOLDOWN_S);
    ESP_LOGI(TAG, "Device    : %s", CONFIG_GAS_DEVICE_NAME);
    ESP_LOGI(TAG, "=====================================");

    ESP_ERROR_CHECK(gas_config_init());
    ESP_ERROR_CHECK(gas_sensor_init());
    ESP_ERROR_CHECK(air_sensor_init());
    ESP_ERROR_CHECK(gas_led_init());

    esp_err_t berr = gas_buzzer_init();
    if (berr != ESP_OK) {
        ESP_LOGE(TAG, "Buzzer init failed: %s (LED alarm still active)",
                 esp_err_to_name(berr));
    }
    esp_err_t ferr = gas_fan_init();
    if (ferr != ESP_OK) {
        ESP_LOGE(TAG, "Fan init failed: %s", esp_err_to_name(ferr));
    }
    esp_err_t verr = gas_valve_init();
    if (verr != ESP_OK) {
        ESP_LOGE(TAG, "Valve servo init failed: %s", esp_err_to_name(verr));
    }

    /* Let the sensor heaters settle before the Wi-Fi radio adds its own
     * current peak: both together can brown out a weak USB supply. */
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_ERROR_CHECK(gas_network_start());

    if (gas_discord_init() != ESP_OK) {
        ESP_LOGW(TAG, "Discord alerts off until a webhook is set (local LED and buzzer still active)");
    }

    esp_err_t merr = gas_mqtt_init();
    if (merr != ESP_OK) {
        ESP_LOGW(TAG, "MQTT init failed: %s (MQTT publishing disabled)",
                 esp_err_to_name(merr));
    }

    const TickType_t cooldown = pdMS_TO_TICKS(CONFIG_GAS_ALARM_COOLDOWN_S * 1000);
    TickType_t last_gas_alert = xTaskGetTickCount() - cooldown;
    TickType_t last_air_alert = xTaskGetTickCount() - cooldown;
    TickType_t last_info = 0;
    TickType_t fan_until = 0;
    bool fan_runon = false;
    bool was_connected = false;

    /* Sensor monitoring + local LED/buzzer alarm run immediately and do not
     * wait for Wi-Fi: a gas leak must be signalled even if the network is down.
     * Discord notifications are best-effort and only sent while connected. */
    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool connected = gas_network_is_connected();
        if (connected && !was_connected) {
            ESP_LOGI(TAG, "Connected to Wi-Fi, IP = %s", gas_network_get_ip());
        }
        was_connected = connected;

        if (gas_control_take_recalibrate()) {
            ESP_LOGW(TAG, "Recalibrating both sensors (keep them in clean air)");
            bool gas_ok = gas_sensor_recalibrate() == ESP_OK;
            bool air_ok = air_sensor_recalibrate() == ESP_OK;
            gas_mqtt_publish_ack("recalibrate", gas_ok && air_ok,
                                 gas_ok && air_ok ? "done"
                                 : !gas_ok && !air_ok ? "both sensors have no power, old values kept"
                                 : !gas_ok ? "MQ-2 has no power, its old value kept"
                                           : "MQ-135 has no power, its old value kept");
        }

        gas_reading_t reading = gas_sensor_read();
        air_reading_t air = air_sensor_read();

        bool sim_discord = true, sim_actuators = true;
        gas_sim_mode_t sim = gas_control_sim(&sim_discord, &sim_actuators);
        if (sim == GAS_SIM_GAS) {
            reading.ppm = CONFIG_GAS_THRESHOLD_PPM * 1.6f;
            reading.mv = SIM_GAS_MV;
            reading.fault = false;
        }
#if CONFIG_AQ_ENABLE
        if (sim == GAS_SIM_AIR) {
            air.co2_ppm = CONFIG_AQ_THRESHOLD_PPM * 1.6f;
            air.mv = SIM_AIR_MV;
            air.fault = false;
        }
#endif
        bool gas_sim = (sim == GAS_SIM_GAS), air_sim = (sim == GAS_SIM_AIR);
        bool alarm = gas_sensor_is_alarm(reading.ppm);
        bool poor_air = air_sensor_is_poor(air.co2_ppm);

        gas_led_set(alarm || poor_air);
        gas_buzzer_set(alarm ? GAS_BUZZER_GAS
                             : (poor_air ? GAS_BUZZER_POOR_AIR
                                         : GAS_BUZZER_OFF));
        gas_mqtt_publish_reading(reading.ppm, reading.mv, alarm, gas_sim, reading.fault);
        gas_mqtt_publish_air_quality(air.co2_ppm, air.mv, poor_air, air_sim, air.fault);

        /* Valve and fan. A simulation moves them only if its command allowed it. */
        bool act_gas = alarm && (!gas_sim || sim_actuators);
        bool act_air = poor_air && (!air_sim || sim_actuators);
        bool valve_was_closed = gas_valve_is_closed();
        int fan_before = gas_fan_get();

        gas_valve_req_t req = gas_control_take_valve_request();
        if (req == GAS_VALVE_REQ_CLOSE) {
            gas_valve_close();
            gas_mqtt_publish_ack("valve", true, "closed");
        } else if (req == GAS_VALVE_REQ_OPEN) {
            if (alarm) {
                gas_mqtt_publish_ack("valve", false, "gas alarm active, valve stays closed");
            } else {
                gas_valve_open();
                gas_mqtt_publish_ack("valve", true, "opened");
            }
        }

        /* the valve latches closed: only an "open" command reopens it */
        bool auto_closed = false;
        if (act_gas && !gas_valve_is_closed()) {
            gas_valve_close();
            auto_closed = true;
        }

        if (act_gas) {
            fan_until = now + pdMS_TO_TICKS(CONFIG_GAS_FAN_RUNON_S * 1000);
            fan_runon = true;
        }
        /* one gas_fan_set() per loop so the kick-start is not restarted every second */
        int fan_pct, test_pct;
        if (act_gas || (fan_runon && (int32_t)(fan_until - now) > 0)) {
            fan_pct = 100;
        } else if (act_air) {
            fan_runon = false;
            fan_pct = CONFIG_GAS_FAN_POOR_PERCENT;
        } else {
            fan_runon = false;
            fan_pct = 0;
        }
        /* a fan test overrides everything except a gas alarm */
        if (!act_gas && gas_control_fan_test(&test_pct)) {
            fan_pct = test_pct;
        }
        gas_fan_set(fan_pct);

        if (gas_valve_is_closed() != valve_was_closed || gas_fan_get() != fan_before) {
            gas_mqtt_publish_info();
        }

        bool may_notify = connected && gas_discord_ready();
        if (alarm) {
            ESP_LOGW(TAG, "!!! GAS ALARM !!!%s ~%d ppm (raw %u mV)",
                     gas_sim ? " [SIM]" : "", (int)reading.ppm, reading.mv);
            if ((now - last_gas_alert) >= cooldown) {
                if (may_notify && (!gas_sim || sim_discord)) {
                    ESP_LOGI(TAG, "Discord gas alarm %s",
                             gas_discord_send_alarm(reading.ppm, reading.mv, gas_sim) ? "sent" : "FAILED");
                }
                gas_mqtt_publish_alarm(reading.ppm, reading.mv);
                last_gas_alert = now;
            }
        }
        if (auto_closed) {
            ESP_LOGW(TAG, "Valve closed automatically%s", gas_sim ? " [SIM]" : "");
            if (may_notify && (!gas_sim || sim_discord)) {
                ESP_LOGI(TAG, "Discord valve notice %s",
                         gas_discord_send_valve_closed(gas_sim) ? "sent" : "FAILED");
            }
        }
        if (!alarm && reading.fault) {
            ESP_LOGW(TAG, "MQ-2 NO SIGNAL (raw %u mV): sensor has no power, check its VCC wire",
                     reading.mv);
        } else if (!alarm) {
            ESP_LOGI(TAG, "OK: ~%d ppm (raw %u mV, threshold %d ppm)",
                     (int)reading.ppm, reading.mv, CONFIG_GAS_THRESHOLD_PPM);
        }

        if (poor_air) {
            ESP_LOGW(TAG, "Air quality POOR%s: ~%d ppm CO2eq (raw %u mV)",
                     air_sim ? " [SIM]" : "", (int)air.co2_ppm, air.mv);
            if ((now - last_air_alert) >= cooldown) {
                if (may_notify && (!air_sim || sim_discord)) {
                    ESP_LOGI(TAG, "Discord air alert %s",
                             gas_discord_send_air(air.co2_ppm, air.mv, air_sim) ? "sent" : "FAILED");
                }
                last_air_alert = now;
            }
        } else if (air.fault) {
            ESP_LOGW(TAG, "MQ-135 NO SIGNAL (raw %u mV): sensor has no power, check its VCC wire",
                     air.mv);
        } else {
            ESP_LOGI(TAG, "Air quality OK: ~%d ppm CO2eq (raw %u mV)",
                     (int)air.co2_ppm, air.mv);
        }

        /* refresh RSSI/IP/simulation state for dashboards every 30 s */
        if (connected && (now - last_info) >= pdMS_TO_TICKS(30000)) {
            gas_mqtt_publish_info();
            last_info = now;
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_GAS_CHECK_PERIOD_MS));
    }
}
