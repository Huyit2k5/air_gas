#ifndef GAS_VALVE_H
#define GAS_VALVE_H

#include <stdbool.h>
#include "esp_err.h"

/* Gas shut-off valve turned by a hobby servo (MG90S/SG90) on GAS_VALVE_GPIO.
 * The open/closed state is kept in NVS, so a reboot puts the servo back where
 * it was instead of reopening a valve that a gas alarm closed. */
esp_err_t gas_valve_init(void);

/* Turn the servo to the closed / open angle. Does nothing if the valve is
 * already there. The PWM stops ~0.7 s later so an idle servo neither hums
 * nor heats up. Call from one task only (the main loop). */
void gas_valve_close(void);
void gas_valve_open(void);

bool gas_valve_is_closed(void);

#endif
