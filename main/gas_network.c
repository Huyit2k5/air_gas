#include "gas_network.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"
#include "sdkconfig.h"
#include "gas_config.h"

static const char *TAG = "GAS_NET";

static bool s_connected = false;
static char s_ip_str[IP4ADDR_STRLEN_MAX] = {0};
static esp_timer_handle_t s_trial_timer;

/* A network saved over MQTT is "on trial" until it connects once. If it never
 * does, go back to the previous network so the device does not get stranded. */
#define WIFI_TRIAL_TIMEOUT_US (60LL * 1000 * 1000)

static void trial_timeout(void *arg)
{
    (void)arg;
    if (!s_connected) {
        ESP_LOGE(TAG, "Wi-Fi \"%s\" did not connect within 60 s, restoring the previous network",
                 gas_config_ssid());
        gas_config_wifi_revert();
        esp_restart();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t event, void *data)
{
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started, connecting to \"%s\"...",
                 gas_config_ssid());
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
        if (gas_config_wifi_trial()) {
            gas_config_wifi_confirm();
            if (s_trial_timer) esp_timer_stop(s_trial_timer);
        }
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

    /* lwIP/TCP-IP stack and the default STA netif must exist before Wi-Fi
     * starts, otherwise any socket/DNS call (e.g. MQTT) asserts "Invalid mbox". */
    err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(err));
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "create default Wi-Fi STA netif failed");
        return ESP_FAIL;
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
    strlcpy((char *)sta.sta.ssid, gas_config_ssid(), sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, gas_config_pass(), sizeof(sta.sta.password));

    err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err != ESP_OK) return err;

    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    /* 11 dBm (units of 0.25 dBm) is plenty indoors and lowers the current
     * peaks that trip the brownout detector on a weak USB supply. */
    esp_wifi_set_max_tx_power(44);

    if (gas_config_wifi_trial()) {
        const esp_timer_create_args_t args = { .callback = trial_timeout, .name = "wifi_trial" };
        if (esp_timer_create(&args, &s_trial_timer) == ESP_OK) {
            esp_timer_start_once(s_trial_timer, WIFI_TRIAL_TIMEOUT_US);
        }
    }

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
