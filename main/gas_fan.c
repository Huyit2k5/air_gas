#include "gas_fan.h"
#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_GAS_FAN_ENABLE

#include "driver/ledc.h"
#include "esp_timer.h"

static const char *TAG = "GAS_FAN";

/* Opto-isolated modules (FR120N/D4184 with a PC817) only follow PWM up to
 * about 1 kHz; a bare logic-level MOSFET can go to 20 kHz (silent). */
#define FAN_MODE LEDC_LOW_SPEED_MODE
#define FAN_TIMER LEDC_TIMER_0
#define FAN_CHANNEL LEDC_CHANNEL_0
#define FAN_FREQ_HZ CONFIG_GAS_FAN_PWM_HZ
#define FAN_RES LEDC_TIMER_10_BIT
#define FAN_DUTY_MAX ((1 << 10) - 1)
#define FAN_KICK_US (500 * 1000)

static int s_target;
static bool s_kicking;
static int64_t s_kick_until_us;

static void apply(int percent)
{
    ledc_set_duty(FAN_MODE, FAN_CHANNEL, (uint32_t)(FAN_DUTY_MAX * percent / 100));
    ledc_update_duty(FAN_MODE, FAN_CHANNEL);
}

esp_err_t gas_fan_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = FAN_MODE,
        .duty_resolution = FAN_RES,
        .timer_num = FAN_TIMER,
        .freq_hz = FAN_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }
    const ledc_channel_config_t ch = {
        .gpio_num = CONFIG_GAS_FAN_GPIO,
        .speed_mode = FAN_MODE,
        .channel = FAN_CHANNEL,
        .timer_sel = FAN_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "Exhaust fan ready on GPIO %d (MOSFET, %d Hz PWM)",
             CONFIG_GAS_FAN_GPIO, FAN_FREQ_HZ);
    return ESP_OK;
}

void gas_fan_set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int64_t now = esp_timer_get_time();

    if (percent == s_target) {
        if (s_kicking && now >= s_kick_until_us) {
            apply(percent);
            s_kicking = false;
        }
        return;
    }
    if (s_target == 0 && percent > 0 && percent < 100) {
        apply(100);
        s_kicking = true;
        s_kick_until_us = now + FAN_KICK_US;
    } else {
        apply(percent);
        s_kicking = false;
    }
    ESP_LOGI(TAG, "Fan %d%% -> %d%%", s_target, percent);
    s_target = percent;
}

int gas_fan_get(void)
{
    return s_target;
}

#else /* !CONFIG_GAS_FAN_ENABLE */

esp_err_t gas_fan_init(void)
{
    ESP_LOGI("GAS_FAN", "Exhaust fan disabled (CONFIG_GAS_FAN_ENABLE=n)");
    return ESP_OK;
}

void gas_fan_set(int percent)
{
    (void)percent;
}

int gas_fan_get(void)
{
    return 0;
}

#endif /* CONFIG_GAS_FAN_ENABLE */
