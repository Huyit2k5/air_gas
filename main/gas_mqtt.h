#ifndef GAS_MQTT_H
#define GAS_MQTT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t gas_mqtt_init(void);

bool gas_mqtt_is_connected(void);

/* sim = true when the value comes from a simulation command, not the sensor;
 * fault = true when the sensor reads as unpowered (VCC wire out or loose) */
void gas_mqtt_publish_reading(float ppm, uint16_t mv, bool alarm, bool sim, bool fault);

void gas_mqtt_publish_alarm(float ppm, uint16_t mv);

void gas_mqtt_publish_air_quality(float co2_ppm, uint16_t mv, bool poor, bool sim, bool fault);

/* airgas/<device>/ack: result of a command received on airgas/<device>/cmd */
void gas_mqtt_publish_ack(const char *cmd, bool ok, const char *msg);

/* airgas/<device>/info (retained): firmware version, IP, RSSI, simulation */
void gas_mqtt_publish_info(void);

#endif
