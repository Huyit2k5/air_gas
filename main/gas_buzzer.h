#ifndef GAS_BUZZER_H
#define GAS_BUZZER_H

#include "esp_err.h"

typedef enum {
    GAS_BUZZER_OFF = 0,
    GAS_BUZZER_POOR_AIR, /* slow beep: air quality above threshold */
    GAS_BUZZER_GAS,      /* fast beep: gas alarm (takes priority) */
} gas_buzzer_mode_t;

/* Configures the GPIO (buzzer stays silent at boot) and starts the beep
 * pattern task. Returns ESP_OK (no-op) when the buzzer is disabled. */
esp_err_t gas_buzzer_init(void);

void gas_buzzer_set(gas_buzzer_mode_t mode);

#endif
