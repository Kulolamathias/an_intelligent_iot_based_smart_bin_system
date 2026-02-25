/**
 * @file bin_network_service.h
 * @brief Bin-to-Bin Network Service – Peer Discovery and Redirect Logic
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service manages peer discovery and nearest‑bin selection. It receives
 * commands from the command router (triggered by the state manager) and
 * processes them in its own task. It emits events (e.g., EVENT_REDIRECT_TO_BIN)
 * via the event dispatcher.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: public API for the service manager (init, register_commands, start).
 * - Does NOT: subscribe to events directly; only receives commands.
 *
 * =============================================================================
 * DEPENDENCIES
 * =============================================================================
 * - mqtt_service (publish/subscribe commands)
 * - gps_service (location retrieval – optional)
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#ifndef BIN_NETWORK_SERVICE_H
#define BIN_NETWORK_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the bin network service.
 *
 * Creates internal queue, timer, and task. Does not start any network activity.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t bin_network_service_init(void);

/**
 * @brief Register command handlers with the command router.
 *
 * This service handles CMD_BIN_NET_NOTIFY_MQTT_CONNECTED,
 * CMD_BIN_NET_NOTIFY_NETWORK_MESSAGE, and CMD_BIN_NET_NOTIFY_LEVEL_UPDATE.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t bin_network_service_register_commands(void);

/**
 * @brief Start the bin network service.
 *
 * Begins the heartbeat timer and prepares to react to commands.
 *
 * @return ESP_OK always.
 */
esp_err_t bin_network_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* BIN_NETWORK_SERVICE_H */