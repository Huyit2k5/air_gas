#ifndef AIR_SENSOR_H
#define AIR_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    uint16_t mv;    /* raw ADC voltage at the pin, for diagnostics */
    float co2_ppm;  /* estimated CO2-equivalent concentration */
    bool fault;     /* sensor reads as unpowered (see SENSOR_NO_POWER_MV); co2_ppm is 0 */
} air_reading_t;

esp_err_t air_sensor_init(void);

air_reading_t air_sensor_read(void);

bool air_sensor_is_poor(float co2_ppm);

/* Measures Ro again (about 5 s, needs normal air) and saves it. Returns
 * ESP_ERR_INVALID_STATE and keeps the old Ro if the sensor reads as unpowered. */
esp_err_t air_sensor_recalibrate(void);

#endif
