/**
 * @file servo_service.h
 * @brief Servo Service – multi‑servo motion manager.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns multiple servo driver handles. It provides smooth motion
 * (angle stepping) for lid open/close and direct angle control.
 *
 * It runs a FreeRTOS task to update positions periodically.
 * =============================================================================
 */

#ifndef SERVO_SERVICE_H
#define SERVO_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise servo service.
 *        Creates driver handles for all configured servos (lid, aux, etc.)
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t servo_service_init(void);

/**
 * @brief Start servo service.
 *        Starts the motion update task.
 * @return ESP_OK on success.
 */
esp_err_t servo_service_start(void);

/**
 * @brief Stop servo service.
 *        Stops the motion task and all servos.
 * @return ESP_OK on success.
 */
esp_err_t servo_service_stop(void);

/**
 * @brief Register command handlers with command_router.
 * @return ESP_OK on success.
 */
esp_err_t servo_service_register_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_SERVICE_H */