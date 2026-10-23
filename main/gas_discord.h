#ifndef GAS_DISCORD_H
#define GAS_DISCORD_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* The webhook comes from gas_config (NVS, set over MQTT) or menuconfig. */
esp_err_t gas_discord_init(void);

bool gas_discord_ready(void);

/* simulated = true prefixes the message with [GIẢ LẬP] */
bool gas_discord_send_alarm(float ppm, uint16_t mv, bool simulated);

bool gas_discord_send_air(float co2_ppm, uint16_t mv, bool simulated);

/* "valve closed automatically" notice, sent right after a gas alarm */
bool gas_discord_send_valve_closed(bool simulated);

#endif
