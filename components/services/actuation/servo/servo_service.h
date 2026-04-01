/**
 * @file components/services/actuation/servo_service/servo_service.h
 * @brief Servo Service – executes lid control commands using servo driver.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service handles commands that control the lid servo motor. It receives
 * commands from the command router (issued by state_manager) and executes
 * them using the servo driver. It posts completion events (e.g., lid opened)
 * back to the core via the event dispatcher.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - CMD_OPEN_LID          – move servo to open angle (e.g., 90°)
 * - CMD_CLOSE_LID         – move servo to closed angle (0°)
 * - CMD_SERVO_SET_ANGLE   – set arbitrary angle (for testing/flexibility)
 * - CMD_SERVO_STOP        – stop PWM (relax servo)
 *
 * =============================================================================
 * EVENTS POSTED
 * =============================================================================
 * - EVENT_LID_OPENED      – after opening movement completes (simulated by timer)
 * - EVENT_LID_CLOSED      – after closing movement completes
 * - EVENT_SERVO_ERROR     – if command fails or movement times out
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-03-30
 */

#ifndef SERVO_SERVICE_H
#define SERVO_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the servo service.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t servo_service_init(void);

/**
 * @brief Register command handlers with the command router.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t servo_service_register_handlers(void);

/**
 * @brief Start the servo service (creates internal timer for movement completion).
 * @return ESP_OK always.
 */
esp_err_t servo_service_start(void);

/**
 * @brief Stop the servo service (deletes internal timer).
 * @return ESP_OK always.
 */
esp_err_t servo_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_SERVICE_H */