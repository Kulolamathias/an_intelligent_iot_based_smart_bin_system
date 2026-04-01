/**
 * @file system_state.c
 * @brief System State Authority – Implementation
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This file implements the single authoritative storage of the system state.
 * It contains absolutely no decision logic – it is a passive record keeper.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - s_current_state is never read or written outside this module.
 * - s_current_state is always a valid system_state_t.
 * - No other module shadows, caches, or duplicates the state value.
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#include "system_state.h"
#include <stddef.h>

/* ============================================================
 * SINGLE AUTHORITATIVE INSTANCE 
 *
 * This static variable is the ONE TRUE SOURCE of the current state.
 * It is never exposed directly; all access is through the public API.
 * ============================================================ */
static system_state_t s_current_state = SYSTEM_STATE_INIT;

/* ============================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================ */

void system_state_init(void)
{
    s_current_state = SYSTEM_STATE_INIT;
}

system_state_t system_state_get(void)
{
    return s_current_state;
}

state_transition_result_t system_state_set(system_state_t new_state)
{
    if (new_state >= SYSTEM_STATE_MAX) {
        return STATE_TRANSITION_INVALID;
    }
    s_current_state = new_state;
    return STATE_TRANSITION_OK;
}

const char* system_state_to_string(system_state_t state)
{
    switch (state) {
        case SYSTEM_STATE_INIT:        return "INIT";
        case SYSTEM_STATE_IDLE:        return "IDLE";
        case SYSTEM_STATE_ACTIVE:      return "ACTIVE";
        case SYSTEM_STATE_NEAR_FULL:   return "NEAR_FULL";
        case SYSTEM_STATE_FULL:        return "FULL";
        case SYSTEM_STATE_ERROR:       return "ERROR";
        case SYSTEM_STATE_MAINTENANCE: return "MAINTENANCE";
        case SYSTEM_STATE_LOW_POWER:   return "LOW_POWER";
        default:                       return "UNKNOWN";
    }
}