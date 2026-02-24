/**
 * @file mqtt_service.h
 * @brief MQTT Service – Manages MQTT Connection Lifecycle
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the MQTT connection state machine. It receives commands
 * from the core (connect, disconnect, publish, subscribe, etc.) and emits
 * events (connected, disconnected, message received) back to the core.
 *
 * It uses the MQTT client abstraction for transport and the topic abstraction
 * for constructing topics.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: public API for the service manager (init, register_handlers, start).
 * - Does NOT: contain any business logic; only transport and state management.
 *
 * =============================================================================
 * DEPENDENCIES
 * =============================================================================
 * - mqtt_client_abstraction (transport layer)
 * - mqtt_topic (topic builder)
 * - service_interfaces (command registration, event posting)
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the MQTT service.
 *
 * Creates the internal queue, timer, and task. Initializes the MQTT client
 * abstraction with a default configuration (later overridden by commands).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mqtt_service_init(void);

/**
 * @brief Register MQTT service command handlers with the command router.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mqtt_service_register_handlers(void);

/**
 * @brief Start the MQTT service.
 *
 * This function does nothing; it is provided for lifecycle consistency with
 * the service manager. The actual work begins when commands are received.
 *
 * @return ESP_OK always.
 */
esp_err_t mqtt_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_SERVICE_H */