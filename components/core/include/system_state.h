/**
 * @file system_state.h
 * @brief System State Authority Module
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module is the SINGLE AUTHORITATIVE OWNER of the current system state.
 * No other module stores, caches, or modifies the system state independently.
 *
 * All state transitions MUST be performed by calling system_state_set().
 * State decisions are made exclusively in state_manager.c; this module
 * only records the decided state.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Owns: current system state (singleton)
 * - Provides: read/write access to state
 * - Guarantees: state value is always a valid system_state_t
 *
 * =============================================================================
 * EXPLICIT NON-RESPONSIBILITIES
 * =============================================================================
 * - Does NOT decide when to change state
 * - Does NOT evaluate events or conditions
 * - Does NOT generate commands
 * - Does NOT contain any transition logic
 * - Does NOT know about other core modules
 *
 * SYSTEM_STATE_ANY is a special sentinel used in transition rules to match
 * any state, avoiding duplication of rules across states.
 * 
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * SYSTEM STATE ENUMERATION
 *
 * These are the only valid operating modes of the system.
 * Each state represents a stable, externally visible condition.
 * ============================================================ */
typedef enum {
    SYSTEM_STATE_INIT = 0,      /**< System starting, hardware not ready */
    SYSTEM_STATE_IDLE,          /**< Waiting for user interaction */
    SYSTEM_STATE_ACTIVE,        /**< User interaction in progress (attention + disposal) */
    SYSTEM_STATE_NEAR_FULL,     /**< Fill level above near‑full threshold */
    SYSTEM_STATE_FULL,          /**< Bin full – locked, redirect active */
    SYSTEM_STATE_MAINTENANCE,   /**< GSM‑authenticated servicing mode */
    SYSTEM_STATE_ERROR,         /**< Fault condition – safe fallback */
    SYSTEM_STATE_LOW_POWER,     /**< Reserved for future power saving */
    SYSTEM_STATE_MAX
} system_state_t;

/* Special value to match any state in transition rules */
#define SYSTEM_STATE_ANY SYSTEM_STATE_MAX

/* ============================================================
 * TRANSITION RESULT (internal use only)
 * ============================================================ */
typedef enum {
    STATE_TRANSITION_OK = 0,       /**< State changed successfully */
    STATE_TRANSITION_INVALID,      /**< New state out of range */
    STATE_TRANSITION_IGNORED       /**< Transition not applied (reserved) */
} state_transition_result_t;

/* ============================================================
 * PUBLIC API – SINGLE SOURCE OF TRUTH
 * ============================================================ */

/**
 * @brief Initialize system state module.
 * 
 * Sets the authoritative state to SYSTEM_STATE_INIT.
 * Must be called once before any state read/write.
 */
void system_state_init(void);

/**
 * @brief Get current system state.
 * @return system_state_t – the one and only current state.
 */
system_state_t system_state_get(void);

/**
 * @brief Set current system state.
 * 
 * This function is the ONLY way to change the system state.
 * Intended for exclusive use by state_manager.c.
 * 
 * @param new_state Valid state (must be < SYSTEM_STATE_MAX).
 * @return state_transition_result_t – OK if changed, INVALID if out of range.
 */
state_transition_result_t system_state_set(system_state_t new_state);

/**
 * @brief Convert state to human-readable string (for logging).
 * @param state State to convert.
 * @return Constant string representation.
 */
const char* system_state_to_string(system_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_STATE_H */