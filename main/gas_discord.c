#include "gas_discord.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "gas_config.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "GAS_DISCORD";

static char s_payload[512];

static bool post_json(const char *url, const char *json)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .buffer_size = 512,
        .buffer_size_tx = 512,
        .timeout_ms = 10000,
        /* Discord is HTTPS only: verify it with the built-in CA bundle */
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "http client init failed");
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, strlen(json));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int w = esp_http_client_write(client, json, strlen(json));
    if (w < 0) {
        ESP_LOGE(TAG, "http write failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int status = 0;
    err = esp_http_client_fetch_headers(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fetch headers failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Discord HTTP status: %d", status);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return (status >= 200 && status < 300);
}

esp_err_t gas_discord_init(void)
{
    if (!gas_discord_ready()) {
        ESP_LOGW(TAG, "No Discord webhook configured (menuconfig GAS_DISCORD_WEBHOOK "
                      "or MQTT command discord_webhook)");
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "Discord webhook ready");
    return ESP_OK;
}

bool gas_discord_ready(void)
{
    return strlen(gas_config_webhook()) > 0;
}

static bool send_text(const char *text)
{
    if (!gas_discord_ready()) return false;
    int n = snprintf(s_payload, sizeof(s_payload), "{\"content\": \"%s\"}", text);
    if (n < 0 || (size_t)n >= sizeof(s_payload)) {
        ESP_LOGE(TAG, "payload too long");
        return false;
    }
    ESP_LOGW(TAG, "Sending Discord message: %s", s_payload);
    return post_json(gas_config_webhook(), s_payload);
}

bool gas_discord_send_alarm(float ppm, uint16_t mv, bool simulated)
{
    char text[256];
    snprintf(text, sizeof(text), "**%s** - %sGAS ALARM **~%d ppm** (threshold %d ppm, raw %u mV)",
             CONFIG_GAS_DEVICE_NAME, simulated ? "[GIẢ LẬP] " : "", (int)ppm,
             CONFIG_GAS_THRESHOLD_PPM, mv);
    return send_text(text);
}

bool gas_discord_send_air(float co2_ppm, uint16_t mv, bool simulated)
{
#if CONFIG_AQ_ENABLE
    int threshold = CONFIG_AQ_THRESHOLD_PPM;
#else
    int threshold = 0;
#endif
    char text[256];
    snprintf(text, sizeof(text), "**%s** - %sKHÔNG KHÍ KÉM **~%d ppm CO2eq** (threshold %d ppm, raw %u mV)",
             CONFIG_GAS_DEVICE_NAME, simulated ? "[GIẢ LẬP] " : "", (int)co2_ppm, threshold, mv);
    return send_text(text);
}
