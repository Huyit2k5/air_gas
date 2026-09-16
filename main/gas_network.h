#ifndef GAS_NETWORK_H
#define GAS_NETWORK_H

#include <stdbool.h>
#include "esp_err.h"
#include "sdkconfig.h"

esp_err_t gas_network_start(void);

bool gas_network_is_connected(void);

const char *gas_network_get_ip(void);

#endif
