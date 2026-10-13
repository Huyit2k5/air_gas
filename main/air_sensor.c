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

/* See gas_sensor.c: drop the first read after a channel switch, then
 * trimmed-mean over several samples to cut ADC noise and cross-channel leak. */
#define ADC_SAMPLES 16
#define ADC_TRIM 4

static int read_raw_filtered(adc_channel_t chan)
{
    adc_oneshot_unit_handle_t adc = gas_sensor_get_adc_unit();
    int samples[ADC_SAMPLES];
    int dummy;
    adc_oneshot_read(adc, chan, &dummy);

    for (int i = 0; i < ADC_SAMPLES; i++) {
        if (adc_oneshot_read(adc, chan, &samples[i]) != ESP_OK) {
            return -1;
        }
    }
    for (int i = 1; i < ADC_SAMPLES; i++) {
        int v = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > v) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = v;
    }
    int sum = 0;
    for (int i = ADC_TRIM; i < ADC_SAMPLES - ADC_TRIM; i++) {
        sum += samples[i];
    }
    return sum / (ADC_SAMPLES - 2 * ADC_TRIM);
}

static uint16_t read_raw_mv(void)
{
    adc_channel_t chan = (adc_channel_t)CONFIG_AQ_ADC_CHANNEL;
    int raw = read_raw_filtered(chan);
    if (raw < 0) {
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

static bool cal_nvs_open(nvs_handle_t *handle)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err == ESP_OK && nvs_open("aq_cal", NVS_READWRITE, handle) == ESP_OK;
}

/* See gas_sensor.c: refuses (keeping the previous Ro) while the sensor reads
 * as unpowered, since an Ro from a dead sensor ruins every later reading. */
static esp_err_t measure_ro(void)
{
    ESP_LOGW(TAG, "Calibrating Ro, assuming the sensor is in normal/well-ventilated "
                  "air (~%dppm CO2). Keep it there for the next %d seconds...",
             (int)CO2_ATMOSPHERIC_PPM,
             (CAL_SAMPLE_COUNT * CAL_SAMPLE_DELAY_MS) / 1000);

    float sum_rs = 0.0f;
    uint32_t sum_mv = 0;
    for (int i = 0; i < CAL_SAMPLE_COUNT; i++) {
        uint16_t mv = read_raw_mv();
        sum_mv += mv;
        sum_rs += compute_rs_ohm(mv);
        vTaskDelay(pdMS_TO_TICKS(CAL_SAMPLE_DELAY_MS));
    }
    uint16_t mv_avg = sum_mv / CAL_SAMPLE_COUNT;
    if (mv_avg < SENSOR_NO_POWER_MV) {
        ESP_LOGE(TAG, "Calibration aborted: MQ-135 reads %u mV, it has no power "
                      "(check its VCC wire). Keeping Ro=%.1f ohm", mv_avg, s_ro_ohm);
        return ESP_ERR_INVALID_STATE;
    }

    float rs_avg = sum_rs / CAL_SAMPLE_COUNT;
    /* Invert the ppm curve at the assumed atmospheric baseline to get Ro. */
    s_ro_ohm = rs_avg * powf(CO2_CURVE_A / CO2_ATMOSPHERIC_PPM, 1.0f / CO2_CURVE_B);
    ESP_LOGI(TAG, "Calibration done: %u mV, Rs_avg=%.1f ohm, Ro=%.1f ohm",
             mv_avg, rs_avg, s_ro_ohm);

    nvs_handle_t handle;
    if (cal_nvs_open(&handle)) {
        nvs_set_blob(handle, "ro_ohm", &s_ro_ohm, sizeof(s_ro_ohm));
        nvs_commit(handle);
        nvs_close(handle);
    } else {
        ESP_LOGW(TAG, "NVS unavailable, calibration will not persist across reboots");
    }
    return ESP_OK;
}

static void load_ro(void)
{
    nvs_handle_t handle;
    if (cal_nvs_open(&handle)) {
        float stored_ro = 0.0f;
        size_t len = sizeof(stored_ro);
        if (nvs_get_blob(handle, "ro_ohm", &stored_ro, &len) == ESP_OK &&
            len == sizeof(stored_ro) && stored_ro > 0.0f) {
            s_ro_ohm = stored_ro;
            ESP_LOGI(TAG, "Loaded MQ-135 calibration from NVS: Ro=%.1f ohm", s_ro_ohm);
        }
        nvs_close(handle);
    }
    if (s_ro_ohm <= 0.0f && measure_ro() != ESP_OK) {
        ESP_LOGW(TAG, "MQ-135 not calibrated yet: power it, wait a few minutes, "
                      "then send {\"recalibrate\":true}");
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

    load_ro();
    return ESP_OK;
}

air_reading_t air_sensor_read(void)
{
    air_reading_t r;
    r.mv = read_raw_mv();
    /* compute_co2_ppm() floors at 400 ppm, which would make an unplugged
     * sensor look like perfectly clean air: report it as a fault instead. */
    r.fault = r.mv < SENSOR_NO_POWER_MV;
    r.co2_ppm = r.fault ? 0.0f : compute_co2_ppm(compute_rs_ohm(r.mv));
    return r;
}

esp_err_t air_sensor_recalibrate(void)
{
    return measure_ro();
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
    air_reading_t r = { .mv = 0, .co2_ppm = 0.0f, .fault = false };
    return r;
}

esp_err_t air_sensor_recalibrate(void)
{
    return ESP_OK;
}

bool air_sensor_is_poor(float co2_ppm)
{
    (void)co2_ppm;
    return false;
}

#endif /* CONFIG_AQ_ENABLE */
