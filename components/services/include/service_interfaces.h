/**
 * @file service_interfaces.h
 * @brief Common service infrastructure – base contract and safe wrappers.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This header defines the base contract that all services must follow,
 * and provides safe, deterministic wrappers for core interaction.
 *
 * Services must NOT include core headers directly; they use these wrappers.
 *
 * =============================================================================
 * @version 1.0.0
 * =============================================================================
 */

#ifndef SERVICE_INTERFACES_H
#define SERVICE_INTERFACES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "event_types.h"           // core event definitions
#include "command_types.h"         // core command definitions
#include "command_router.h"        // for handler registration
#include "event_dispatcher.h"      // for posting events

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * BASE SERVICE CONTRACT
 *
 * Every service SHOULD implement these functions.
 * They are called by the application layer during system startup.
 * ============================================================ */

/**
 * @brief Initialize service (allocate resources, init drivers, etc.)
 * @return ESP_OK on success, error code otherwise.
 */
typedef esp_err_t (*service_init_fn)(void);

/**
 * @brief Start service (begin background tasks, timers, etc.)
 * @return ESP_OK on success, error code otherwise.
 */
typedef esp_err_t (*service_start_fn)(void);

/**
 * @brief Stop service (suspend tasks, power down, etc.)
 * @return ESP_OK on success, error code otherwise.
 */
typedef esp_err_t (*service_stop_fn)(void);

/**
 * @brief Register all command handlers for this service with command_router.
 * @return ESP_OK on success, error code otherwise.
 */
typedef esp_err_t (*service_register_handlers_fn)(void);

/* ============================================================
 * SAFE EVENT POSTING WRAPPER
 *
 * Services MUST use this function instead of calling
 * event_dispatcher_post_event directly.
 * It adds service‑level logging and can be extended with
 * debugging or filtering in the future.
 * ============================================================ */

/**
 * @brief Post an event to the core.
 * @param event Pointer to event structure.
 * @return ESP_OK on success, error code otherwise.
 */
static inline esp_err_t service_post_event(const system_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Optional: add service‑level logging here (e.g., ESP_LOGD)
    return event_dispatcher_post_event(event);
}

/* ============================================================
 * SAFE COMMAND HANDLER REGISTRATION WRAPPER
 *
 * Services MUST use this function to register command handlers.
 * It centralises error checking and can enforce naming conventions.
 * ============================================================ */

/**
 * @brief Register a handler for a command.
 * @param cmd       Command ID.
 * @param handler   Function pointer.
 * @param context   Service context (may be NULL).
 * @return ESP_OK on success, error code otherwise.
 */
static inline esp_err_t service_register_command(
    system_command_id_t cmd,
    esp_err_t (*handler)(void *context, void *params),
    void *context)
{
    // Optional: validate that cmd is within range? (router does anyway)
    return command_router_register_handler(cmd, handler, context);
}

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_INTERFACES_H */