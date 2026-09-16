#ifndef GAS_LED_H
#define GAS_LED_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t gas_led_init(void);

void gas_led_set(bool on);

#endif
