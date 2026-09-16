#include "gas_sensor.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "GAS_SENSOR";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_cali_enabled = false;

static bool calibration_init(adc_unit_t unit, adc_channel_t chan,
                              adc_atten_t atten, adc_cali_handle_t *out_handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = chan,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, out_handle) == ESP_OK) {
        return true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, out_handle) == ESP_OK) {
        return true;
    }
#endif
    return false;
}

esp_err_t gas_sensor_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(err));
        return err;
    }

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_channel_t chan = (adc_channel_t)CONFIG_GAS_ADC_CHANNEL;
    err = adc_oneshot_config_channel(s_adc_handle, chan, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(err));
        return err;
    }

    s_cali_enabled = calibration_init(ADC_UNIT_1, chan, ADC_ATTEN_DB_12,
                                       &s_cali_handle);
    if (s_cali_enabled) {
        ESP_LOGI(TAG, "ADC calibration enabled");
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable, using raw value");
    }

    ESP_LOGI(TAG, "Gas sensor ready on ADC1_CH%d (GPIO %d)",
             CONFIG_GAS_ADC_CHANNEL, CONFIG_GAS_ADC_PIN);
    return ESP_OK;
}

uint16_t gas_sensor_read_mv(void)
{
    adc_channel_t chan = (adc_channel_t)CONFIG_GAS_ADC_CHANNEL;
    int raw;
    if (adc_oneshot_read(s_adc_handle, chan, &raw) != ESP_OK) {
        return 0;
    }

    int mv;
    if (s_cali_enabled &&
        adc_cali_raw_to_voltage(s_cali_handle, raw, &mv) == ESP_OK) {
        if (mv < 0) mv = 0;
        if (mv > 3300) mv = 3300;
    } else {
        mv = (raw * 3300) / 4095;
    }
    return (uint16_t)mv;
}

bool gas_sensor_is_alarm(uint16_t mv)
{
    return mv >= CONFIG_GAS_THRESHOLD_MV;
}
