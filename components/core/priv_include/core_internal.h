/**
 * @file components/core/priv_include/core_internal.h
 * @brief Internal types and structures for core module.
 * @note This file is PRIVATE to the core module only.
 * @author Mathias Kulola
 * @date 2024-12-23
 * @version 1.0.0
 */

#ifndef CORE_INTERNAL_H
#define CORE_INTERNAL_H

#include "system_state.h"
#include "event_types.h"
#include "command_types.h"

/**
 * @brief Single state transition rule.
 *
 * Defines: "When in [current_state] and [event] occurs,
 *           transition to [next_state] and execute [command]."
 */
typedef struct
{
    system_state_t      current_state;   /**< State we are in */
    system_event_id_t   event;           /**< Event that triggers transition */
    system_state_t      next_state;      /**< State to transition to */
    system_command_id_t command;         /**< Command to execute on transition */

} state_transition_rule_t;

/**
 * @brief System initialization parameters.
 */
typedef struct
{
    uint8_t near_full_threshold;     /**< Percentage for near-full warning (default: 80) */
    uint8_t full_threshold;          /**< Percentage for full state (default: 95) */
    uint8_t empty_threshold;         /**< Percentage for empty state (default: 10) */
    uint32_t intent_timeout_ms;      /**< Intent detection timeout in ms (default: 5000) */
    uint32_t escalation_timeout_ms;  /**< Escalation timeout in ms (default: 900000 = 15min) */
    uint32_t periodic_report_ms;     /**< Periodic report interval in ms (default: 300000 = 5min) */
    uint32_t lid_timeout_ms;         /**< Lid auto-close timeout in ms (default: 10000) */
} system_init_params_t;

/**
 * @brief System statistics for monitoring.
 */
typedef struct
{
    uint32_t total_usage_count;      /**< Total number of times bin was used */
    uint32_t error_count;            /**< Total number of errors encountered */
    uint32_t last_maintenance_time;  /**< Timestamp of last maintenance */
    uint32_t total_waste_collected;  /**< Estimated waste collected (units) */
    uint32_t total_notifications;    /**< Total notifications sent */
} system_stats_t;

/**
 * @brief Internal system context (extends public context).
 */
typedef struct
{
    system_context_t public_ctx;     /**< Public context (exposed via API) */
    system_stats_t stats;            /**< System statistics */
    system_init_params_t params;     /**< System parameters */
    uint32_t state_entry_time;       /**< Time when current state was entered */
    uint32_t last_activity_time;     /**< Time of last user activity */
} system_context_internal_t;

/* ============================================================
 * INTERNAL HELPER MACROS
 * ============================================================ */

/**
 * @brief Check if current fill level indicates near-full state.
 */
#define IS_NEAR_FULL(level, threshold) ((level) >= (threshold))

/**
 * @brief Check if current fill level indicates full state.
 */
#define IS_FULL(level, threshold) ((level) >= (threshold))

/**
 * @brief Check if current fill level indicates empty state.
 */
#define IS_EMPTY(level, threshold) ((level) <= (threshold))

/**
 * @brief Calculate time spent in current state.
 */
#define TIME_IN_STATE() (xTaskGetTickCount() * portTICK_PERIOD_MS - g_system_context_internal.state_entry_time)

/**
 * @brief Update state entry time (call on state transition).
 */
#define UPDATE_STATE_ENTRY_TIME() (g_system_context_internal.state_entry_time = xTaskGetTickCount() * portTICK_PERIOD_MS)

/**
 * @brief Update last activity time.
 */
#define UPDATE_LAST_ACTIVITY() (g_system_context_internal.last_activity_time = xTaskGetTickCount() * portTICK_PERIOD_MS)

#endif /* CORE_INTERNAL_H */