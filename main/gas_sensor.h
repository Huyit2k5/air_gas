#ifndef GAS_SENSOR_H
#define GAS_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t gas_sensor_init(void);

uint16_t gas_sensor_read_mv(void);

bool gas_sensor_is_alarm(uint16_t mv);

#endif
