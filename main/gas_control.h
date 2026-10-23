#ifndef GAS_CONTROL_H
#define GAS_CONTROL_H

#include <stdbool.h>
#include <stddef.h>

#define GAS_FW_VERSION "1.4.0"

typedef enum {
    GAS_SIM_OFF = 0,
    GAS_SIM_GAS,   /* pretend MQ-2 sees a gas leak */
    GAS_SIM_AIR,   /* pretend MQ-135 sees poor air */
} gas_sim_mode_t;

typedef enum {
    GAS_VALVE_REQ_NONE = 0,
    GAS_VALVE_REQ_OPEN,
    GAS_VALVE_REQ_CLOSE,
} gas_valve_req_t;

/* Handles one JSON command received on airgas/<device>/cmd (called from the
 * MQTT task). Replies on airgas/<device>/ack. Commands:
 *   {"sim":"gas"|"air"|"off","duration":60,"discord":true,"actuators":true}
 *   {"valve":"open"|"close"}                   open is refused during a gas alarm
 *   {"fan_test":60,"duration":10}              fan at 0-100 % for 5-60 s, 0 stops; a gas alarm still wins
 *   {"wifi":{"ssid":"...","pass":"..."}}       saved, then restart
 *   {"broker":"mqtt://192.168.1.14:1883"}      saved, then restart
 *   {"discord_webhook":"https://discord.com/api/webhooks/..."}
 *   {"recalibrate":true} */
void gas_control_handle_json(const char *json, size_t len);

/* Current simulation mode; expires automatically after its duration.
 * *discord tells whether a simulated alarm may be sent to Discord,
 * *actuators whether it may move the valve and run the fan. Either may be NULL. */
gas_sim_mode_t gas_control_sim(bool *discord, bool *actuators);
const char *gas_control_sim_name(void);

/* True once after a "recalibrate" command; the main loop does the work. */
bool gas_control_take_recalibrate(void);

/* Pending "valve" command, returned once; the main loop decides and acks. */
gas_valve_req_t gas_control_take_valve_request(void);

/* True while a "fan_test" command is running; *percent gets its speed. */
bool gas_control_fan_test(int *percent);

#endif
