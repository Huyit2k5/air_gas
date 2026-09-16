#include "gas_sensor.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "GAS_SENSOR";

/* MQ-2 curve fit constants (Rs/Ro vs ppm, power regression from the Hanwei
 * MQ-2 datasheet LPG curve): ppm = LPG_CURVE_A * (Rs/Ro) ^ LPG_CURVE_B */
#define LPG_CURVE_A 574.25f
#define LPG_CURVE_B (-2.222f)
/* Rs/Ro ratio in clean air, per the MQ-2 datasheet reference curve */
#define RO_CLEAN_AIR_FACTOR 9.83f

#define CAL_SAMPLE_COUNT 50
#define CAL_SAMPLE_DELAY_MS 100

static adc_oneshot_unit_handle_t s_adc_handle;
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

/* Reconstructs the sensor's Rs (ohm) from the ADC voltage, accounting for
 * any external voltage divider placed before the ADC pin (see
 * GAS_ADC_DIVIDER_PERCENT) and the module's own load resistor RL. */
static float compute_rs_ohm(uint16_t adc_mv)
{
    float aout_mv = (float)adc_mv * 100.0f / (float)CONFIG_GAS_ADC_DIVIDER_PERCENT;
    if (aout_mv < 1.0f) {
        aout_mv = 1.0f;
    }
    if (aout_mv > CONFIG_GAS_SENSOR_VCC_MV) {
        aout_mv = CONFIG_GAS_SENSOR_VCC_MV;
    }
    return (float)CONFIG_GAS_LOAD_RESISTOR_OHM *
           (CONFIG_GAS_SENSOR_VCC_MV - aout_mv) / aout_mv;
}

static float compute_ppm(float rs_ohm)
{
    if (s_ro_ohm <= 0.0f) {
        return 0.0f;
    }
    float ratio = rs_ohm / s_ro_ohm;
    if (ratio <= 0.0f) {
        ratio = 0.0001f;
    }
    return LPG_CURVE_A * powf(ratio, LPG_CURVE_B);
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
        have_handle = (nvs_open("gas_cal", NVS_READWRITE, &handle) == ESP_OK);
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
        ESP_LOGI(TAG, "Loaded MQ-2 calibration from NVS: Ro=%.1f ohm", s_ro_ohm);
        nvs_close(handle);
        return;
    }

    ESP_LOGW(TAG, "No saved calibration -- calibrating Ro now, assuming the "
                  "sensor is currently in CLEAN AIR (no gas). Keep it there "
                  "for the next %d seconds...",
             (CAL_SAMPLE_COUNT * CAL_SAMPLE_DELAY_MS) / 1000);

    float sum_rs = 0.0f;
    for (int i = 0; i < CAL_SAMPLE_COUNT; i++) {
        sum_rs += compute_rs_ohm(read_raw_mv());
        vTaskDelay(pdMS_TO_TICKS(CAL_SAMPLE_DELAY_MS));
    }
    float rs_avg = sum_rs / CAL_SAMPLE_COUNT;
    s_ro_ohm = rs_avg / RO_CLEAN_AIR_FACTOR;
    ESP_LOGI(TAG, "Calibration done: Rs_avg=%.1f ohm, Ro=%.1f ohm",
             rs_avg, s_ro_ohm);

    if (have_handle) {
        nvs_set_blob(handle, "ro_ohm", &s_ro_ohm, sizeof(s_ro_ohm));
        nvs_commit(handle);
        nvs_close(handle);
    }
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

    calibrate_ro();
    return ESP_OK;
}

gas_reading_t gas_sensor_read(void)
{
    gas_reading_t r;
    r.mv = read_raw_mv();
    r.ppm = compute_ppm(compute_rs_ohm(r.mv));
    return r;
}

bool gas_sensor_is_alarm(float ppm)
{
    return ppm >= CONFIG_GAS_THRESHOLD_PPM;
}
