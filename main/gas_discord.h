#ifndef GAS_DISCORD_H
#define GAS_DISCORD_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t gas_discord_init(void);

bool gas_discord_send_alarm(uint16_t mv);

#endif
