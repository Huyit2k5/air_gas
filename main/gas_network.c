#include "gas_network.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"
#include "sdkconfig.h"

static const char *TAG = "GAS_NET";

static bool s_connected = false;
static char s_ip_str[IP4ADDR_STRLEN_MAX] = {0};

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t event, void *data)
{
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started, connecting to \"%s\"...",
                 CONFIG_GAS_WIFI_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT &&
               event == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        const wifi_event_sta_disconnected_t *dis =
            (const wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "Disconnected (reason=%d), auto-retry", dis->reason);
        esp_wifi_connect();
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        s_connected = true;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR,
                 IP2STR(&evt->ip_info.ip));
    }
}

esp_err_t gas_network_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop create failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     &on_wifi_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     &on_wifi_event, NULL);
    if (err != ESP_OK) return err;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, CONFIG_GAS_WIFI_SSID, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, CONFIG_GAS_WIFI_PASSWORD,
            sizeof(sta.sta.password));

    err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err != ESP_OK) return err;

    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Wi-Fi STA started");
    return ESP_OK;
}

bool gas_network_is_connected(void)
{
    return s_connected;
}

const char *gas_network_get_ip(void)
{
    return s_ip_str;
}
