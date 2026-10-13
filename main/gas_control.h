#ifndef GAS_CONTROL_H
#define GAS_CONTROL_H

#include <stdbool.h>
#include <stddef.h>

#define GAS_FW_VERSION "1.2.0"

typedef enum {
    GAS_SIM_OFF = 0,
    GAS_SIM_GAS,   /* pretend MQ-2 sees a gas leak */
    GAS_SIM_AIR,   /* pretend MQ-135 sees poor air */
} gas_sim_mode_t;

/* Handles one JSON command received on airgas/<device>/cmd (called from the
 * MQTT task). Replies on airgas/<device>/ack. Commands:
 *   {"sim":"gas"|"air"|"off","duration":60,"discord":true}
 *   {"wifi":{"ssid":"...","pass":"..."}}       saved, then restart
 *   {"broker":"mqtt://192.168.1.14:1883"}      saved, then restart
 *   {"discord_webhook":"https://discord.com/api/webhooks/..."}
 *   {"recalibrate":true} */
void gas_control_handle_json(const char *json, size_t len);

/* Current simulation mode; expires automatically after its duration.
 * *discord tells whether a simulated alarm may be sent to Discord. */
gas_sim_mode_t gas_control_sim(bool *discord);
const char *gas_control_sim_name(void);

/* True once after a "recalibrate" command; the main loop does the work. */
bool gas_control_take_recalibrate(void);

#endif
