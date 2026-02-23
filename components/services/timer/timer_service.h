/**
 * @file timer_service.h
 * @brief Timer Service – manages FreeRTOS software timers.
 *
 * =============================================================================
 * This service owns three software timers:
 *   - Intent timer
 *   - Escalation timer
 *   - Periodic report timer
 *
 * It responds to start/stop commands and posts timeout events when timers expire.
 * No business logic – pure timer management.
 * =============================================================================
 */

#ifndef TIMER_SERVICE_H
#define TIMER_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise timer service (creates FreeRTOS timers).
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t timer_service_init(void);

/**
 * @brief Start timer service (no action – timers are started via commands).
 * @return ESP_OK.
 */
esp_err_t timer_service_start(void);

/**
 * @brief Stop timer service (stops all timers).
 * @return ESP_OK.
 */
esp_err_t timer_service_stop(void);

/**
 * @brief Register command handlers with command_router.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t timer_service_register_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* TIMER_SERVICE_H */