#ifndef GAS_SENSOR_H
#define GAS_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

typedef struct {
    uint16_t mv;  /* raw ADC voltage at the pin, for diagnostics */
    float ppm;    /* estimated LPG concentration from the MQ-2 Rs/Ro curve */
} gas_reading_t;

esp_err_t gas_sensor_init(void);

gas_reading_t gas_sensor_read(void);

bool gas_sensor_is_alarm(float ppm);

/* Shared ADC1 unit handle, claimed by gas_sensor_init(). Other ADC1-based
 * sensors (e.g. air_sensor.c) must reuse this instead of claiming their own
 * unit -- ADC_UNIT_1 can only be claimed by one adc_oneshot_new_unit() call
 * at a time. Only valid after gas_sensor_init() has returned ESP_OK. */
adc_oneshot_unit_handle_t gas_sensor_get_adc_unit(void);

#endif
