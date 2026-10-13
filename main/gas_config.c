#include "gas_config.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "GAS_CFG";
#define NS "netcfg"

static char s_ssid[33];
static char s_pass[65];
static char s_broker[128];
static char s_webhook[200];
static bool s_trial;

static void load_str(bool has, nvs_handle_t h, const char *key,
                     char *dst, size_t cap, const char *fallback)
{
    size_t len = cap;
    if (has && nvs_get_str(h, key, dst, &len) == ESP_OK && dst[0] != '\0') {
        return;
    }
    strlcpy(dst, fallback, cap);
}

esp_err_t gas_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t h = 0;
    /* ESP_ERR_NVS_NOT_FOUND here just means nothing has been saved yet */
    bool has = (nvs_open(NS, NVS_READONLY, &h) == ESP_OK);
    load_str(has, h, "ssid", s_ssid, sizeof(s_ssid), CONFIG_GAS_WIFI_SSID);
    load_str(has, h, "pass", s_pass, sizeof(s_pass), CONFIG_GAS_WIFI_PASSWORD);
    load_str(has, h, "broker", s_broker, sizeof(s_broker), CONFIG_GAS_MQTT_BROKER_URI);
    load_str(has, h, "webhook", s_webhook, sizeof(s_webhook), CONFIG_GAS_DISCORD_WEBHOOK);
    uint8_t trial = 0;
    if (has) {
        nvs_get_u8(h, "trial", &trial);
        nvs_close(h);
    }
    s_trial = (trial != 0);

    ESP_LOGI(TAG, "Wi-Fi \"%s\"%s, broker %s, Discord %s", s_ssid,
             s_trial ? " (new, on trial)" : "",
             s_broker[0] ? s_broker : "(disabled)",
             s_webhook[0] ? "configured" : "NOT SET");
    return ESP_OK;
}

const char *gas_config_ssid(void) { return s_ssid; }
const char *gas_config_pass(void) { return s_pass; }
const char *gas_config_broker(void) { return s_broker; }
const char *gas_config_webhook(void) { return s_webhook; }
bool gas_config_wifi_trial(void) { return s_trial; }

static esp_err_t open_rw(nvs_handle_t *h)
{
    esp_err_t err = nvs_open(NS, NVS_READWRITE, h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t gas_config_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) return err;
    /* the network in use right now (saved or menuconfig) becomes the backup */
    nvs_set_str(h, "bk_ssid", s_ssid);
    nvs_set_str(h, "bk_pass", s_pass);
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_set_u8(h, "trial", 1);
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "Saved new Wi-Fi \"%s\" (previous \"%s\" kept as backup)", ssid, s_ssid);
    return err;
}

esp_err_t gas_config_set_broker(const char *uri)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "broker", uri);
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "Saved new MQTT broker %s", uri);
    return err;
}

esp_err_t gas_config_set_webhook(const char *url)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "webhook", url);
    err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        strlcpy(s_webhook, url, sizeof(s_webhook));   /* takes effect immediately */
        ESP_LOGW(TAG, "Saved new Discord webhook");
    }
    return err;
}

void gas_config_wifi_confirm(void)
{
    if (!s_trial) return;
    nvs_handle_t h;
    if (open_rw(&h) != ESP_OK) return;
    nvs_set_u8(h, "trial", 0);
    nvs_commit(h);
    nvs_close(h);
    s_trial = false;
    ESP_LOGI(TAG, "New Wi-Fi \"%s\" confirmed", s_ssid);
}

void gas_config_wifi_revert(void)
{
    nvs_handle_t h;
    if (open_rw(&h) != ESP_OK) return;
    char ssid[33] = {0}, pass[65] = {0};
    size_t ls = sizeof(ssid), lp = sizeof(pass);
    if (nvs_get_str(h, "bk_ssid", ssid, &ls) == ESP_OK && ssid[0] != '\0') {
        nvs_get_str(h, "bk_pass", pass, &lp);
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
    } else {
        /* no backup saved: fall back to the menuconfig network */
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "pass");
    }
    nvs_set_u8(h, "trial", 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "Restored previous Wi-Fi \"%s\"", ssid[0] ? ssid : CONFIG_GAS_WIFI_SSID);
}
