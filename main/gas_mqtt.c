#include "gas_mqtt.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "GAS_MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static char s_topic_reading[96];
static char s_topic_alarm[96];
static char s_topic_status[96];

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connected to MQTT broker");
        esp_mqtt_client_publish(s_client, s_topic_status, "online", 0, 1, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected from MQTT broker");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

esp_err_t gas_mqtt_init(void)
{
    if (strlen(CONFIG_GAS_MQTT_BROKER_URI) == 0) {
        ESP_LOGW(TAG, "No MQTT broker configured, MQTT publishing disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }

    snprintf(s_topic_reading, sizeof(s_topic_reading), "airgas/%s/reading",
             CONFIG_GAS_DEVICE_NAME);
    snprintf(s_topic_alarm, sizeof(s_topic_alarm), "airgas/%s/alarm",
             CONFIG_GAS_DEVICE_NAME);
    snprintf(s_topic_status, sizeof(s_topic_status), "airgas/%s/status",
             CONFIG_GAS_DEVICE_NAME);

    /* Last Will: broker publishes this (retained) if the client disconnects
     * ungracefully, so Node-RED can show the device as offline without
     * waiting for a reading to time out. */
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_GAS_MQTT_BROKER_URI,
        .credentials.client_id = CONFIG_GAS_DEVICE_NAME,
        .session.last_will = {
            .topic = s_topic_status,
            .msg = "offline",
            .msg_len = 0,
            .qos = 1,
            .retain = 1,
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register event failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started, broker=%s", CONFIG_GAS_MQTT_BROKER_URI);
    return ESP_OK;
}

bool gas_mqtt_is_connected(void)
{
    return s_connected;
}

void gas_mqtt_publish_reading(float ppm, uint16_t mv, bool alarm)
{
    if (!s_connected) {
        return;
    }
    char payload[128];
    int n = snprintf(payload, sizeof(payload),
                      "{\"ppm\":%d,\"mv\":%u,\"threshold\":%d,\"alarm\":%s}",
                      (int)ppm, mv, CONFIG_GAS_THRESHOLD_PPM,
                      alarm ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        return;
    }
    esp_mqtt_client_publish(s_client, s_topic_reading, payload, 0, 0, 0);
}

void gas_mqtt_publish_alarm(float ppm, uint16_t mv)
{
    if (!s_connected) {
        return;
    }
    char payload[160];
    int n = snprintf(payload, sizeof(payload),
                      "{\"ppm\":%d,\"mv\":%u,\"threshold\":%d,\"device\":\"%s\"}",
                      (int)ppm, mv, CONFIG_GAS_THRESHOLD_PPM,
                      CONFIG_GAS_DEVICE_NAME);
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        return;
    }
    esp_mqtt_client_publish(s_client, s_topic_alarm, payload, 0, 1, 0);
}
