/**
 * @file state_manager.h
 * @brief State Manager – Sole System Decision Engine
 *
 * =============================================================================
 * ARCHITECTURAL DOCTRINE
 * =============================================================================
 * THIS MODULE IS THE ONLY AUTHORITY FOR SYSTEM BEHAVIOR.
 *
 * - It OWNS the transition table and all decision logic.
 * - It INTERPRETS events (facts) and PRODUCES commands (actions).
 * - It UPDATES system context based on observed facts.
 *
 * NO OTHER MODULE MAY:
 *   - Decide when to change state.
 *   - Generate commands.
 *   - Interpret events at a system level.
 *   - Modify system context outside state_manager.
 *
 * =============================================================================
 * DETERMINISM GUARANTEES
 * =============================================================================
 * 1. The same event, in the same state, with the same context,
 *    ALWAYS produces the same transition and command batch.
 * 2. Transition conditions read ONLY from system_context_t.
 * 3. Event order is preserved by the event_dispatcher.
 * 4. No polling – system reacts only to events.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Owns: system_context_t, transition table, command batching.
 * - Uses: system_state module (authoritative state storage).
 * - Uses: command_router module (command dispatch).
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "event_types.h"
#include "system_state.h"
#include "command_types.h"
#include "core_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * SYSTEM CONTEXT
 *
 * Stores all observed facts and current operating parameters.
 * This is the ONLY persistent data store in the core.
 * All decision conditions are evaluated against this context.
 * ============================================================ */
typedef struct {
    /* --------------------------------------------------------
     * Fill level and bin lock state
     * -------------------------------------------------------- */
    uint8_t bin_fill_level_percent;     /**< 0-100%, last measurement */
    bool bin_locked;                    /**< true = lid disabled */
    bool gps_valid;                     /**< true if last GPS fix was valid */

    /* --------------------------------------------------------
     * Configurable thresholds (runtime adjustable)
     * -------------------------------------------------------- */
    struct {
        uint8_t full_threshold;       /**< Level to trigger STATE_FULL */
        uint8_t near_full_threshold;  /**< Level for warning notification */
        uint8_t empty_threshold;      /**< Level to clear full state */
    } params;

    /* --------------------------------------------------------
     * Extended context – feature placeholders
     * -------------------------------------------------------- */
    gps_coordinates_t gps_coordinates; /**< Last known position */
    bool pending_welcome;              /**< true after lid close, waiting to show welcome */
    auth_status_t auth_status;         /**< Current authentication state */
    error_flags_t error_flags;         /**< Active error conditions */

} system_context_t;

/* ============================================================
 * PUBLIC API
 * ============================================================ */

/**
 * @brief Initialize state manager with initial context.
 * @param initial_context Pointer to initial context values.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t state_manager_init(const system_context_t *initial_context);

/**
 * @brief Process an incoming event.
 *
 * 1. Updates context with any new observation data from the event.
 * 2. Evaluates transition table in declared order.
 * 3. If a rule matches: changes state, executes command batch.
 *
 * @param event Event to process.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t state_manager_process_event(const system_event_t *event);

/**
 * @brief Get read-only pointer to current system context.
 * @return Const pointer to system_context_t.
 */
const system_context_t* state_manager_get_context(void);

/**
 * @brief Copy the current system context into a caller‑supplied buffer.
 *
 * Thread‑safe; uses an internal mutex.
 *
 * @param[out] dest  Pointer to a system_context_t structure to fill.
 *                   Must not be NULL.
 */
void state_manager_copy_context(system_context_t *dest);

#ifdef __cplusplus
}
#endif

#endif /* STATE_MANAGER_H */