#include "gas_control.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "gas_config.h"
#include "gas_mqtt.h"

static const char *TAG = "GAS_CTRL";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static gas_sim_mode_t s_sim = GAS_SIM_OFF;
static int64_t s_sim_until_us;
static bool s_sim_discord = true;
static volatile bool s_recal;
static esp_timer_handle_t s_restart_timer;

static void restart_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

/* restart a moment later so the ack has time to reach the broker */
static void schedule_restart(void)
{
    if (s_restart_timer == NULL) {
        const esp_timer_create_args_t args = { .callback = restart_cb, .name = "cmd_restart" };
        if (esp_timer_create(&args, &s_restart_timer) != ESP_OK) {
            esp_restart();
        }
    }
    esp_timer_start_once(s_restart_timer, 1500 * 1000);
}

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void handle_sim(const cJSON *root, const char *mode)
{
    gas_sim_mode_t m;
    if (strcmp(mode, "gas") == 0) m = GAS_SIM_GAS;
    else if (strcmp(mode, "air") == 0) m = GAS_SIM_AIR;
    else if (strcmp(mode, "off") == 0) m = GAS_SIM_OFF;
    else { gas_mqtt_publish_ack("sim", false, "mode must be gas, air or off"); return; }

    int duration = 60;
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "duration");
    if (cJSON_IsNumber(d)) duration = (int)d->valuedouble;
    if (duration < 5) duration = 5;
    if (duration > 600) duration = 600;
    const cJSON *dc = cJSON_GetObjectItemCaseSensitive(root, "discord");
    bool discord = cJSON_IsBool(dc) ? cJSON_IsTrue(dc) : true;

    taskENTER_CRITICAL(&s_lock);
    s_sim = m;
    s_sim_until_us = esp_timer_get_time() + (int64_t)duration * 1000000;
    s_sim_discord = discord;
    taskEXIT_CRITICAL(&s_lock);

    char msg[64];
    if (m == GAS_SIM_OFF) snprintf(msg, sizeof(msg), "simulation off");
    else snprintf(msg, sizeof(msg), "%s for %d s, discord %s", mode, duration, discord ? "on" : "off");
    ESP_LOGW(TAG, "Simulation: %s", msg);
    gas_mqtt_publish_ack("sim", true, msg);
    gas_mqtt_publish_info();
}

static void handle_wifi(const cJSON *wifi)
{
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(wifi, "pass");
    const char *p = cJSON_IsString(pass) ? pass->valuestring : "";
    if (!cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0 || strlen(ssid->valuestring) > 32) {
        gas_mqtt_publish_ack("wifi", false, "ssid must be 1-32 characters");
        return;
    }
    size_t pl = strlen(p);
    if (pl != 0 && (pl < 8 || pl > 63)) {
        gas_mqtt_publish_ack("wifi", false, "password must be empty or 8-63 characters");
        return;
    }
    if (gas_config_set_wifi(ssid->valuestring, p) != ESP_OK) {
        gas_mqtt_publish_ack("wifi", false, "could not save to NVS");
        return;
    }
    gas_mqtt_publish_ack("wifi", true, "saved, restarting; falls back to the old network if the new one fails within 60 s");
    schedule_restart();
}

static void handle_broker(const char *uri)
{
    if (!(starts_with(uri, "mqtt://") || starts_with(uri, "mqtts://")) || strlen(uri) >= 128) {
        gas_mqtt_publish_ack("broker", false, "use mqtt://host:port");
        return;
    }
    if (gas_config_set_broker(uri) != ESP_OK) {
        gas_mqtt_publish_ack("broker", false, "could not save to NVS");
        return;
    }
    gas_mqtt_publish_ack("broker", true, "saved, restarting");
    schedule_restart();
}

static void handle_webhook(const char *url)
{
    bool ok_prefix = starts_with(url, "https://discord.com/api/webhooks/") ||
                     starts_with(url, "https://discordapp.com/api/webhooks/");
    if (!ok_prefix || strlen(url) >= 200) {
        gas_mqtt_publish_ack("discord_webhook", false, "not a Discord webhook URL");
        return;
    }
    if (gas_config_set_webhook(url) != ESP_OK) {
        gas_mqtt_publish_ack("discord_webhook", false, "could not save to NVS");
        return;
    }
    gas_mqtt_publish_ack("discord_webhook", true, "saved");
}

void gas_control_handle_json(const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL || !cJSON_IsObject(root)) {
        gas_mqtt_publish_ack("?", false, "invalid JSON");
        cJSON_Delete(root);
        return;
    }
    const cJSON *it;
    if (cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(root, "sim"))) {
        handle_sim(root, it->valuestring);
    } else if (cJSON_IsObject(it = cJSON_GetObjectItemCaseSensitive(root, "wifi"))) {
        handle_wifi(it);
    } else if (cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(root, "broker"))) {
        handle_broker(it->valuestring);
    } else if (cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(root, "discord_webhook"))) {
        handle_webhook(it->valuestring);
    } else if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "recalibrate"))) {
        s_recal = true;
        gas_mqtt_publish_ack("recalibrate", true, "recalibrating, keep the sensors in clean air for about 10 s");
    } else {
        gas_mqtt_publish_ack("?", false, "unknown command");
    }
    cJSON_Delete(root);
}

gas_sim_mode_t gas_control_sim(bool *discord)
{
    bool expired = false;
    taskENTER_CRITICAL(&s_lock);
    if (s_sim != GAS_SIM_OFF && esp_timer_get_time() > s_sim_until_us) {
        s_sim = GAS_SIM_OFF;
        expired = true;
    }
    gas_sim_mode_t m = s_sim;
    bool d = s_sim_discord;
    taskEXIT_CRITICAL(&s_lock);
    if (expired) {
        ESP_LOGW(TAG, "Simulation ended");
        gas_mqtt_publish_info();
    }
    if (discord) *discord = d;
    return m;
}

const char *gas_control_sim_name(void)
{
    gas_sim_mode_t m = gas_control_sim(NULL);
    return m == GAS_SIM_GAS ? "gas" : (m == GAS_SIM_AIR ? "air" : "off");
}

bool gas_control_take_recalibrate(void)
{
    bool r = s_recal;
    s_recal = false;
    return r;
}
