/**
 * @file command_router.h
 * @brief Command Router – Pure Dispatch Mechanism
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module is a stateless, deterministic router.
 *
 * It maps command IDs to registered handler functions.
 * It contains NO business logic, NO policy, and NO state.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Owns: handler registry (static array).
 * - Provides: registration and execution services.
 * - Does NOT: decide which commands to execute.
 * - Does NOT: interpret command parameters.
 * - Does NOT: depend on any service or driver.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * 1. Each command can have at most ONE registered handler.
 * 2. Attempting to register a handler twice returns ESP_ERR_INVALID_STATE.
 * 3. Executing an unregistered command returns ESP_ERR_NOT_FOUND.
 * 4. Handlers are invoked synchronously.
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#ifndef COMMAND_ROUTER_H
#define COMMAND_ROUTER_H

#include "command_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize command router.
 * 
 * Clears the handler registry. Must be called before any registration.
 * @return ESP_OK on success.
 */
esp_err_t command_router_init(void);

/**
 * @brief Execute a system command.
 * 
 * Routes command to registered handler. If no handler is registered,
 * returns ESP_ERR_NOT_FOUND.
 * 
 * @param command Command ID.
 * @param params Optional parameters (may be NULL). Must be castable to the
 *               correct parameter struct for the command.
 * @return ESP_OK if handler executed successfully, error otherwise.
 */
esp_err_t command_router_execute(system_command_id_t command, void *params);

/**
 * @brief Register a handler for a command.
 * 
 * @param command Command ID.
 * @param handler Function pointer to handler.
 * @param context Service context (passed to handler on each call).
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already registered.
 */
esp_err_t command_router_register_handler(
    system_command_id_t command,
    esp_err_t (*handler)(void *context, void *params),
    void *context
);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_ROUTER_H */