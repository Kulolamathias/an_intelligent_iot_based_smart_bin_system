/**
 * @file wifi_service.h
 * @brief WiFi Service – Manages WiFi Connection Lifecycle
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the WiFi connection state machine. It receives commands
 * from the core (connect, disconnect, set config, get status) and emits events
 * (connected, disconnected, got IP, connection failed) back to the core.
 *
 * It uses the WiFi driver abstraction for low‑level control.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: public API for the service manager (init, register_handlers, start).
 * - Does NOT: contain any business logic; only state and transport management.
 *
 * =============================================================================
 * DEPENDENCIES
 * =============================================================================
 * - wifi_driver_abstraction (low‑level driver)
 * - service_interfaces (command registration, event posting)
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the WiFi service.
 *
 * Creates the internal queue, timer, and task. Initializes the WiFi driver
 * with default configuration (later overridden by CMD_SET_CONFIG_WIFI).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t wifi_service_init(void);

/**
 * @brief Register WiFi service command handlers with the command router.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t wifi_service_register_handlers(void);

/**
 * @brief Start the WiFi service.
 *
 * This function does nothing; it is provided for lifecycle consistency with
 * the service manager. The actual work begins when commands are received.
 *
 * @return ESP_OK always.
 */
esp_err_t wifi_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SERVICE_H */