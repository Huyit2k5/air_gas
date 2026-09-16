#ifndef GAS_SENSOR_H
#define GAS_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    uint16_t mv;  /* raw ADC voltage at the pin, for diagnostics */
    float ppm;    /* estimated LPG concentration from the MQ-2 Rs/Ro curve */
} gas_reading_t;

esp_err_t gas_sensor_init(void);

gas_reading_t gas_sensor_read(void);

bool gas_sensor_is_alarm(float ppm);

#endif
