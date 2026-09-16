#ifndef AIR_SENSOR_H
#define AIR_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    uint16_t mv;    /* raw ADC voltage at the pin, for diagnostics */
    float co2_ppm;  /* estimated CO2-equivalent concentration */
} air_reading_t;

esp_err_t air_sensor_init(void);

air_reading_t air_sensor_read(void);

bool air_sensor_is_poor(float co2_ppm);

#endif
