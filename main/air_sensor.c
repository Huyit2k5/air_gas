#include "air_sensor.h"
#include "sdkconfig.h"
#include "gas_sensor.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "AIR_SENSOR";

#if CONFIG_AQ_ENABLE

/* MQ-135 CO2-equivalent curve fit (Rs/Ro vs ppm, power regression widely
 * cited from the Hanwei MQ-135 datasheet): ppm = A * (Rs/Ro) ^ -B */
#define CO2_CURVE_A 116.6020682f
#define CO2_CURVE_B (-2.769034857f)
/* MQ-135, unlike MQ-2, can never read a true "zero" baseline in normal air:
 * outdoor/well-ventilated air already carries this much CO2. Calibration
 * assumes the sensor sits in air at (approximately) this concentration. */
#define CO2_ATMOSPHERIC_PPM 400.0f

#define CAL_SAMPLE_COUNT 50
#define CAL_SAMPLE_DELAY_MS 100

static adc_cali_handle_t s_cali_handle = NULL;
static bool s_cali_enabled = false;
static float s_ro_ohm = 0.0f;

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

static uint16_t read_raw_mv(void)
{
    adc_channel_t chan = (adc_channel_t)CONFIG_AQ_ADC_CHANNEL;
    int raw;
    if (adc_oneshot_read(gas_sensor_get_adc_unit(), chan, &raw) != ESP_OK) {
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

static float compute_rs_ohm(uint16_t adc_mv)
{
    float aout_mv = (float)adc_mv * 100.0f / (float)CONFIG_AQ_ADC_DIVIDER_PERCENT;
    if (aout_mv < 1.0f) {
        aout_mv = 1.0f;
    }
    if (aout_mv > CONFIG_AQ_SENSOR_VCC_MV) {
        aout_mv = CONFIG_AQ_SENSOR_VCC_MV;
    }
    return (float)CONFIG_AQ_LOAD_RESISTOR_OHM *
           (CONFIG_AQ_SENSOR_VCC_MV - aout_mv) / aout_mv;
}

static float compute_co2_ppm(float rs_ohm)
{
    if (s_ro_ohm <= 0.0f) {
        return 0.0f;
    }
    float ratio = rs_ohm / s_ro_ohm;
    if (ratio <= 0.0f) {
        ratio = 0.0001f;
    }
    float ppm = CO2_CURVE_A * powf(ratio, CO2_CURVE_B);
    if (ppm < CO2_ATMOSPHERIC_PPM) {
        ppm = CO2_ATMOSPHERIC_PPM;
    }
    return ppm;
}

static void calibrate_ro(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    nvs_handle_t handle = 0;
    bool have_handle = false;
    if (err == ESP_OK) {
        have_handle = (nvs_open("aq_cal", NVS_READWRITE, &handle) == ESP_OK);
    }
    if (!have_handle) {
        ESP_LOGW(TAG, "NVS unavailable, calibration will not persist across reboots");
    }

    float stored_ro = 0.0f;
    size_t len = sizeof(stored_ro);
    if (have_handle &&
        nvs_get_blob(handle, "ro_ohm", &stored_ro, &len) == ESP_OK &&
        len == sizeof(stored_ro) && stored_ro > 0.0f) {
        s_ro_ohm = stored_ro;
        ESP_LOGI(TAG, "Loaded MQ-135 calibration from NVS: Ro=%.1f ohm", s_ro_ohm);
        nvs_close(handle);
        return;
    }

    ESP_LOGW(TAG, "No saved calibration -- calibrating Ro now, assuming the "
                  "sensor is currently in normal/well-ventilated air "
                  "(~%dppm CO2). Keep it there for the next %d seconds...",
             (int)CO2_ATMOSPHERIC_PPM,
             (CAL_SAMPLE_COUNT * CAL_SAMPLE_DELAY_MS) / 1000);

    float sum_rs = 0.0f;
    for (int i = 0; i < CAL_SAMPLE_COUNT; i++) {
        sum_rs += compute_rs_ohm(read_raw_mv());
        vTaskDelay(pdMS_TO_TICKS(CAL_SAMPLE_DELAY_MS));
    }
    float rs_avg = sum_rs / CAL_SAMPLE_COUNT;
    /* Invert the ppm curve at the assumed atmospheric baseline to get Ro. */
    s_ro_ohm = rs_avg * powf(CO2_CURVE_A / CO2_ATMOSPHERIC_PPM, 1.0f / CO2_CURVE_B);
    ESP_LOGI(TAG, "Calibration done: Rs_avg=%.1f ohm, Ro=%.1f ohm",
             rs_avg, s_ro_ohm);

    if (have_handle) {
        nvs_set_blob(handle, "ro_ohm", &s_ro_ohm, sizeof(s_ro_ohm));
        nvs_commit(handle);
        nvs_close(handle);
    }
}

esp_err_t air_sensor_init(void)
{
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_channel_t chan = (adc_channel_t)CONFIG_AQ_ADC_CHANNEL;
    esp_err_t err = adc_oneshot_config_channel(gas_sensor_get_adc_unit(),
                                                chan, &chan_cfg);
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

    ESP_LOGI(TAG, "Air quality sensor ready on ADC1_CH%d (GPIO %d)",
             CONFIG_AQ_ADC_CHANNEL, CONFIG_AQ_ADC_PIN);

    calibrate_ro();
    return ESP_OK;
}

air_reading_t air_sensor_read(void)
{
    air_reading_t r;
    r.mv = read_raw_mv();
    r.co2_ppm = compute_co2_ppm(compute_rs_ohm(r.mv));
    return r;
}

bool air_sensor_is_poor(float co2_ppm)
{
    return co2_ppm >= CONFIG_AQ_THRESHOLD_PPM;
}

#else /* !CONFIG_AQ_ENABLE */

esp_err_t air_sensor_init(void)
{
    ESP_LOGI(TAG, "MQ-135 air quality sensor disabled (CONFIG_AQ_ENABLE=n)");
    return ESP_OK;
}

air_reading_t air_sensor_read(void)
{
    air_reading_t r = { .mv = 0, .co2_ppm = 0.0f };
    return r;
}

bool air_sensor_is_poor(float co2_ppm)
{
    (void)co2_ppm;
    return false;
}

#endif /* CONFIG_AQ_ENABLE */
