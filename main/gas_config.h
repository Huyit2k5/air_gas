#ifndef GAS_CONFIG_H
#define GAS_CONFIG_H

#include <stdbool.h>
#include "esp_err.h"

/* Runtime settings that can be changed over MQTT without reflashing.
 * Stored in NVS namespace "netcfg"; each falls back to its menuconfig value
 * when nothing has been saved yet. */

/* Initialises NVS and loads the settings. Call once, before Wi-Fi/MQTT. */
esp_err_t gas_config_init(void);

const char *gas_config_ssid(void);
const char *gas_config_pass(void);
const char *gas_config_broker(void);
const char *gas_config_webhook(void);

/* Saves new Wi-Fi credentials and keeps the current ones as a backup.
 * The new network is "on trial" until it connects once; if it does not,
 * gas_network.c restores the backup (see gas_config_wifi_revert). */
esp_err_t gas_config_set_wifi(const char *ssid, const char *pass);
esp_err_t gas_config_set_broker(const char *uri);
esp_err_t gas_config_set_webhook(const char *url);

bool gas_config_wifi_trial(void);
void gas_config_wifi_confirm(void);
void gas_config_wifi_revert(void);

#endif
