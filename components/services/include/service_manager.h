/**
 * @file components/services/include/service_manager.h
 * @brief Service Manager – orchestrates deterministic service startup.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module owns the static list of all services and enforces a fixed
 * initialization, registration, and start order. It ensures that:
 *   - All services are initialised before any handlers are registered.
 *   - All handlers are registered before any service is started.
 *   - Failure in any step aborts the remaining steps and returns an error.
 *
 * This centralises boot orchestration and prevents services from being
 * started in an inconsistent order.
 * =============================================================================
 */

#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise all services in the predefined order.
 * @return ESP_OK if all services initialised successfully, otherwise the first error.
 */
esp_err_t service_manager_init_all(void);

/**
 * @brief Register command handlers for all services.
 * @return ESP_OK if all registrations succeeded, otherwise the first error.
 */
esp_err_t service_manager_register_all(void);

/**
 * @brief Start all services.
 * @return ESP_OK if all started successfully, otherwise the first error.
 */
esp_err_t service_manager_start_all(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_MANAGER_H */