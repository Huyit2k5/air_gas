#ifndef GAS_FAN_H
#define GAS_FAN_H

#include "esp_err.h"

/* DC exhaust fan switched by a MOSFET module (FR120N/D4184 with opto, or a
 * logic-level LR7843) on GAS_FAN_GPIO. The fan is powered from the separate
 * 5V supply, never from the ESP32. */
esp_err_t gas_fan_init(void);

/* Fan speed 0-100 %. Call it every loop: a start below 100 % is kicked at
 * full speed for a moment first, because small fans stall at low PWM. */
void gas_fan_set(int percent);

/* Last requested speed (0-100). */
int gas_fan_get(void);

#endif
