/**
 * @file components/services/actuation/buzzer_service/buzzer_service.h
 * @brief Buzzer Service – manages buzzer instances and patterns.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service provides a command interface to control buzzers using the
 * buzzer driver. It receives commands from the command router and executes
 * them on the appropriate buzzer instance. It handles single beeps,
 * continuous tones, and predefined patterns.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - CMD_BUZZER_ON         – start continuous tone (frequency, duty)
 * - CMD_BUZZER_OFF        – stop tone
 * - CMD_BUZZER_BEEP       – single beep (duration, frequency, duty)
 * - CMD_BUZZER_PATTERN    – play a predefined pattern
 * - CMD_BUZZER_STOP       – stop any ongoing pattern/beep
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-01
 */

#ifndef BUZZER_SERVICE_H
#define BUZZER_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the buzzer service (creates buzzer instances).
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t buzzer_service_init(void);

/**
 * @brief Register command handlers with the command router.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t buzzer_service_register_handlers(void);

/**
 * @brief Start the buzzer service (no background tasks).
 * @return ESP_OK always.
 */
esp_err_t buzzer_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_SERVICE_H */