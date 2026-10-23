#include "gas_mqtt.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "gas_config.h"
#include "gas_control.h"
#include "gas_fan.h"
#include "gas_network.h"
#include "gas_valve.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "GAS_MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static char s_topic_reading[96];
static char s_topic_alarm[96];
static char s_topic_status[96];
static char s_topic_aq_reading[96];
static char s_topic_cmd[96];
static char s_topic_ack[96];
static char s_topic_info[96];
static char s_cmd_buf[512];

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
        esp_mqtt_client_subscribe(s_client, s_topic_cmd, 1);
        gas_mqtt_publish_info();
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected from MQTT broker");
        break;
    case MQTT_EVENT_DATA:
        /* commands are small: only handle messages that arrive in one piece */
        if (event->topic_len == (int)strlen(s_topic_cmd) &&
            strncmp(event->topic, s_topic_cmd, event->topic_len) == 0) {
            if (event->current_data_offset != 0 || event->data_len != event->total_data_len ||
                event->data_len >= (int)sizeof(s_cmd_buf)) {
                gas_mqtt_publish_ack("?", false, "command too long");
                break;
            }
            memcpy(s_cmd_buf, event->data, event->data_len);
            s_cmd_buf[event->data_len] = '\0';
            ESP_LOGI(TAG, "Command received (%d bytes)", event->data_len);
            gas_control_handle_json(s_cmd_buf, event->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "MQTT transport error: esp_err=%s sock_errno=%d",
                     esp_err_to_name(event->error_handle->esp_tls_last_esp_err),
                     event->error_handle->esp_transport_sock_errno);
        } else if (event->error_handle->error_type ==
                   MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "MQTT connection refused, return_code=%d",
                     event->error_handle->connect_return_code);
        } else {
            ESP_LOGE(TAG, "MQTT error, type=%d", event->error_handle->error_type);
        }
        break;
    default:
        break;
    }
}

esp_err_t gas_mqtt_init(void)
{
    const char *uri = gas_config_broker();
    if (strlen(uri) == 0) {
        ESP_LOGW(TAG, "No MQTT broker configured, MQTT publishing disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }

    snprintf(s_topic_reading, sizeof(s_topic_reading), "airgas/%s/reading", CONFIG_GAS_DEVICE_NAME);
    snprintf(s_topic_alarm, sizeof(s_topic_alarm), "airgas/%s/alarm", CONFIG_GAS_DEVICE_NAME);
    snprintf(s_topic_status, sizeof(s_topic_status), "airgas/%s/status", CONFIG_GAS_DEVICE_NAME);
    snprintf(s_topic_aq_reading, sizeof(s_topic_aq_reading), "airquality/%s/reading", CONFIG_GAS_DEVICE_NAME);
    snprintf(s_topic_cmd, sizeof(s_topic_cmd), "airgas/%s/cmd", CONFIG_GAS_DEVICE_NAME);
    snprintf(s_topic_ack, sizeof(s_topic_ack), "airgas/%s/ack", CONFIG_GAS_DEVICE_NAME);
    snprintf(s_topic_info, sizeof(s_topic_info), "airgas/%s/info", CONFIG_GAS_DEVICE_NAME);

    /* Last Will: broker publishes this (retained) if the client disconnects
     * ungracefully, so dashboards can show the device as offline without
     * waiting for a reading to time out. */
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
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

    ESP_LOGI(TAG, "MQTT client started, broker=%s, commands on %s", uri, s_topic_cmd);
    return ESP_OK;
}

bool gas_mqtt_is_connected(void)
{
    return s_connected;
}

void gas_mqtt_publish_reading(float ppm, uint16_t mv, bool alarm, bool sim, bool fault)
{
    if (!s_connected) {
        return;
    }
    char payload[160];
    int n = snprintf(payload, sizeof(payload),
                      "{\"ppm\":%d,\"mv\":%u,\"threshold\":%d,\"alarm\":%s,\"sim\":%s,\"fault\":%s}",
                      (int)ppm, mv, CONFIG_GAS_THRESHOLD_PPM,
                      alarm ? "true" : "false", sim ? "true" : "false",
                      fault ? "true" : "false");
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

void gas_mqtt_publish_air_quality(float co2_ppm, uint16_t mv, bool poor, bool sim, bool fault)
{
    if (!s_connected) {
        return;
    }
#if CONFIG_AQ_ENABLE
    int threshold = CONFIG_AQ_THRESHOLD_PPM;
#else
    int threshold = 0;
#endif
    char payload[160];
    int n = snprintf(payload, sizeof(payload),
                      "{\"co2_ppm\":%d,\"mv\":%u,\"threshold\":%d,\"poor\":%s,\"sim\":%s,\"fault\":%s}",
                      (int)co2_ppm, mv, threshold, poor ? "true" : "false",
                      sim ? "true" : "false", fault ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        return;
    }
    esp_mqtt_client_publish(s_client, s_topic_aq_reading, payload, 0, 0, 0);
}

void gas_mqtt_publish_ack(const char *cmd, bool ok, const char *msg)
{
    if (!s_connected) {
        return;
    }
    char payload[256];
    int n = snprintf(payload, sizeof(payload), "{\"cmd\":\"%s\",\"ok\":%s,\"msg\":\"%s\"}",
                     cmd, ok ? "true" : "false", msg);
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        return;
    }
    esp_mqtt_client_publish(s_client, s_topic_ack, payload, 0, 1, 0);
}

void gas_mqtt_publish_info(void)
{
    if (!s_connected) {
        return;
    }
    int rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }
    char payload[256];
    int n = snprintf(payload, sizeof(payload),
                     "{\"fw\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"ssid\":\"%s\",\"sim\":\"%s\","
                     "\"valve\":\"%s\",\"fan\":%d}",
                     GAS_FW_VERSION, gas_network_get_ip(), rssi, gas_config_ssid(),
                     gas_control_sim_name(), gas_valve_is_closed() ? "closed" : "open",
                     gas_fan_get());
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        return;
    }
    esp_mqtt_client_publish(s_client, s_topic_info, payload, 0, 1, 1);
}
