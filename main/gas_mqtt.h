#ifndef GAS_MQTT_H
#define GAS_MQTT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t gas_mqtt_init(void);

bool gas_mqtt_is_connected(void);

void gas_mqtt_publish_reading(float ppm, uint16_t mv, bool alarm);

void gas_mqtt_publish_alarm(float ppm, uint16_t mv);

void gas_mqtt_publish_air_quality(float co2_ppm, uint16_t mv, bool poor);

#endif
