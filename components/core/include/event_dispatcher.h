/**
 * @file event_dispatcher.h
 * @brief Event Dispatcher – Central Event Queue & Forwarding
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module provides a thread-safe, ISR-capable event queue.
 *
 * It is the ONLY entry point for events into the core.
 * It forwards events to state_manager for processing.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Owns: FreeRTOS queue and dispatcher task.
 * - Provides: event posting API.
 * - Does NOT: interpret, filter, or prioritize events.
 * - Does NOT: contain any business logic.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * 1. Events are delivered in FIFO order.
 * 2. No event is silently dropped (queue full is logged as error).
 * 3. ISR-safe posting preserves FreeRTOS scheduling semantics.
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#include "event_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize event dispatcher.
 * 
 * Creates internal queue and dispatcher task.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t event_dispatcher_init(void);

/**
 * @brief Post event from task context.
 * 
 * Copies event into queue. Returns ESP_FAIL if queue is full.
 * @param event Event to post.
 * @return ESP_OK on success, ESP_FAIL if queue full.
 */
esp_err_t event_dispatcher_post_event(const system_event_t *event);

/**
 * @brief Post event from ISR context.
 * 
 * ISR-safe version. Must be called with pxHigherPriorityTaskWoken
 * correctly initialized.
 * 
 * @param event Event to post.
 * @param pxHigherPriorityTaskWoken FreeRTOS ISR flag.
 * @return ESP_OK on success, ESP_FAIL if queue full.
 */
esp_err_t event_dispatcher_post_event_from_isr(
    const system_event_t *event,
    BaseType_t *pxHigherPriorityTaskWoken);

/**
 * @brief Get internal event queue handle (for diagnostics only).
 * @return Queue handle, or NULL if not initialized.
 */
QueueHandle_t event_dispatcher_get_queue(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_DISPATCHER_H */