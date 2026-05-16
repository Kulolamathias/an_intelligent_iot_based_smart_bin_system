/**
 * @file ultrasonic_service.h
 * @brief Ultrasonic Service – manages fill-level and intention sensors.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the two ultrasonic sensor instances:
 *   - Fill-level sensor (inside bin, measures distance to trash)
 *   - Intention sensor (tilted, detects close-range presence)
 *
 * It provides:
 *   - On‑demand fill measurement (CMD_READ_FILL_LEVEL)
 *   - Continuous monitoring of intention sensor (posts events on state change)
 *
 * It uses the handle-based ultrasonic driver and contains NO system policy.
 * =============================================================================
 */

#ifndef ULTRASONIC_SERVICE_H
#define ULTRASONIC_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise ultrasonic service.
 *        Creates driver handles for both sensors using Kconfig defaults.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t ultrasonic_service_init(void);

/**
 * @brief Start ultrasonic service.
 *        Begins periodic monitoring of the intention sensor.
 * @return ESP_OK on success.
 */
esp_err_t ultrasonic_service_start(void);

/**
 * @brief Stop ultrasonic service.
 *        Stops intention sensor monitoring.
 * @return ESP_OK on success.
 */
esp_err_t ultrasonic_service_stop(void);

/**
 * @brief Register command handlers with command_router.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t ultrasonic_service_register_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* ULTRASONIC_SERVICE_H */