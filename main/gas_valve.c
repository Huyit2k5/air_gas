#include "gas_valve.h"
#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_GAS_VALVE_ENABLE

#include "driver/ledc.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "GAS_VALVE";

/* Standard hobby servo: 50 Hz frame, 0.5-2.4 ms pulse for 0-180 degrees */
#define SERVO_MODE LEDC_LOW_SPEED_MODE
#define SERVO_TIMER LEDC_TIMER_1
#define SERVO_CHANNEL LEDC_CHANNEL_1
#define SERVO_FREQ_HZ 50
#define SERVO_RES_BITS 14
#define SERVO_MIN_US 500
#define SERVO_MAX_US 2400
#define SERVO_MOVE_US (700 * 1000)

static bool s_closed;
static esp_timer_handle_t s_idle_timer;

static void idle_cb(void *arg)
{
    (void)arg;
    ledc_stop(SERVO_MODE, SERVO_CHANNEL, 0);
}

static void move_to(int deg)
{
    uint32_t pulse_us = SERVO_MIN_US + (uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * deg / 180;
    uint32_t duty = (pulse_us * (1u << SERVO_RES_BITS)) / (1000000 / SERVO_FREQ_HZ);
    ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty);
    ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
    esp_timer_stop(s_idle_timer);
    esp_timer_start_once(s_idle_timer, SERVO_MOVE_US);
}

static void save_state(void)
{
    nvs_handle_t h;
    if (nvs_open("valve", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "closed", s_closed ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

esp_err_t gas_valve_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = SERVO_MODE,
        .duty_resolution = SERVO_RES_BITS,
        .timer_num = SERVO_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }
    const ledc_channel_config_t ch = {
        .gpio_num = CONFIG_GAS_VALVE_GPIO,
        .speed_mode = SERVO_MODE,
        .channel = SERVO_CHANNEL,
        .timer_sel = SERVO_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        return err;
    }
    const esp_timer_create_args_t args = { .callback = idle_cb, .name = "servo_idle" };
    err = esp_timer_create(&args, &s_idle_timer);
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t h;
    uint8_t closed = 0;
    if (nvs_open("valve", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "closed", &closed);
        nvs_close(h);
    }
    s_closed = closed != 0;
    move_to(s_closed ? CONFIG_GAS_VALVE_CLOSED_DEG : CONFIG_GAS_VALVE_OPEN_DEG);
    ESP_LOGI(TAG, "Valve servo ready on GPIO %d, valve %s (restored from NVS)",
             CONFIG_GAS_VALVE_GPIO, s_closed ? "CLOSED" : "open");
    return ESP_OK;
}

void gas_valve_close(void)
{
    if (s_closed) return;
    s_closed = true;
    move_to(CONFIG_GAS_VALVE_CLOSED_DEG);
    save_state();
    ESP_LOGW(TAG, "Valve CLOSED");
}

void gas_valve_open(void)
{
    if (!s_closed) return;
    s_closed = false;
    move_to(CONFIG_GAS_VALVE_OPEN_DEG);
    save_state();
    ESP_LOGW(TAG, "Valve opened");
}

bool gas_valve_is_closed(void)
{
    return s_closed;
}

#else /* !CONFIG_GAS_VALVE_ENABLE */

esp_err_t gas_valve_init(void)
{
    ESP_LOGI("GAS_VALVE", "Valve servo disabled (CONFIG_GAS_VALVE_ENABLE=n)");
    return ESP_OK;
}

void gas_valve_close(void) {}
void gas_valve_open(void) {}

bool gas_valve_is_closed(void)
{
    return false;
}

#endif /* CONFIG_GAS_VALVE_ENABLE */
