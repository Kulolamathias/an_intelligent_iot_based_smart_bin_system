/**
 * @file event_dispatcher.c
 * @brief Event Dispatcher – Implementation
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the FreeRTOS queue and dedicated dispatcher task.
 * No event filtering, no prioritization, no business logic.
 *
 * =============================================================================
 * LIFECYCLE
 * =============================================================================
 * 1. event_dispatcher_init() – creates the queue (task not yet running).
 * 2. After all services are started, event_dispatcher_start() – creates the task.
 *    This ensures events are only dispatched after the system is ready.
 *
 * =============================================================================
 * @author Core Architecture Group
 * @date 2026-03-29
 */

#include "event_dispatcher.h"
#include "state_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define EVENT_QUEUE_LENGTH      32
#define DISPATCHER_STACK_SIZE   16384
#define DISPATCHER_PRIORITY     (tskIDLE_PRIORITY + 5)

static const char *TAG = "EventDispatcher";

static QueueHandle_t g_event_queue = NULL;
static TaskHandle_t  g_dispatcher_task = NULL;
static bool          g_started = false;

static void event_dispatcher_task(void *pvParameters)
{
    (void)pvParameters;
    system_event_t event;

    ESP_LOGI(TAG, "Event dispatcher task started");

    while (1) {
        if (xQueueReceive(g_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Dispatching event: %d", event.id);
            esp_err_t ret = state_manager_process_event(&event);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Event %d processing returned %d", event.id, ret);
            }
        }
    }
}

esp_err_t event_dispatcher_init(void)
{
    if (g_event_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    g_event_queue = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(system_event_t));
    if (g_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_FAIL;
    }

    g_started = false;
    ESP_LOGI(TAG, "Event dispatcher initialized (queue created)");
    return ESP_OK;
}

esp_err_t event_dispatcher_start(void)
{
    if (g_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_started) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xTaskCreate(
        event_dispatcher_task,
        "EventDispatcher",
        DISPATCHER_STACK_SIZE,
        NULL,
        DISPATCHER_PRIORITY,
        &g_dispatcher_task);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create dispatcher task");
        return ESP_FAIL;
    }

    g_started = true;
    ESP_LOGI(TAG, "Event dispatcher started (task running)");
    return ESP_OK;
}

esp_err_t event_dispatcher_post_event(const system_event_t *event)
{
    if (g_event_queue == NULL || !g_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xQueueSend(g_event_queue, event, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t event_dispatcher_post_event_from_isr(
    const system_event_t *event,
    BaseType_t *pxHigherPriorityTaskWoken)
{
    if (g_event_queue == NULL || !g_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xQueueSendFromISR(g_event_queue, event,
                          pxHigherPriorityTaskWoken) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

QueueHandle_t event_dispatcher_get_queue(void)
{
    return g_event_queue;
}



// /**
//  * @file event_dispatcher.c
//  * @brief Event Dispatcher – Implementation
//  *
//  * =============================================================================
//  * ARCHITECTURAL ROLE
//  * =============================================================================
//  * Implements the FreeRTOS queue and dedicated dispatcher task.
//  * No event filtering, no prioritization, no business logic.
//  *
//  * =============================================================================
//  * INVARIANTS
//  * =============================================================================
//  * - Dispatcher task runs at fixed priority (idle+5).
//  * - Queue length is fixed at compile time (32 events).
//  * - Event structure is copied by value – no pointers are stored.
//  *
//  * =============================================================================
//  * @version 1.0.0
//  * @author Core Architecture Group
//  * =============================================================================
//  */

// #include "event_dispatcher.h"
// #include "state_manager.h"
// #include "esp_log.h"
// #include "freertos/task.h"

// #define EVENT_QUEUE_LENGTH   32
// #define DISPATCHER_STACK_SIZE 4096
// #define DISPATCHER_PRIORITY  (tskIDLE_PRIORITY + 5)

// static const char *TAG = "EventDispatcher";

// /* ============================================================
//  * INTERNAL STATE
//  * ============================================================ */
// static QueueHandle_t g_event_queue = NULL;
// static TaskHandle_t  g_dispatcher_task = NULL;

// /* ============================================================
//  * DISPATCHER TASK
//  *
//  * Waits indefinitely for events, forwards each to state_manager.
//  * Never exits – system must not delete this task.
//  * ============================================================ */
// static void event_dispatcher_task(void *pvParameters)
// {
//     (void)pvParameters;
//     system_event_t event;

//     ESP_LOGI(TAG, "Event dispatcher task started");

//     while (1) {
//         if (xQueueReceive(g_event_queue, &event, portMAX_DELAY) == pdTRUE) {
//             ESP_LOGI(TAG, "Dispatching event: %d", event.id);
//             esp_err_t ret = state_manager_process_event(&event);
//             if (ret != ESP_OK) {
//                 ESP_LOGW(TAG, "Event %d processing returned %d", event.id, ret);
//             }
//         }
//     }
// }

// /* ============================================================
//  * PUBLIC API IMPLEMENTATION
//  * ============================================================ */

// esp_err_t event_dispatcher_init(void)
// {
//     if (g_event_queue != NULL) {
//         return ESP_ERR_INVALID_STATE;
//     }

//     g_event_queue = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(system_event_t));
//     if (g_event_queue == NULL) {
//         ESP_LOGE(TAG, "Failed to create event queue");
//         return ESP_FAIL;
//     }

//     BaseType_t ret = xTaskCreate(
//         event_dispatcher_task,
//         "EventDispatcher",
//         DISPATCHER_STACK_SIZE,
//         NULL,
//         DISPATCHER_PRIORITY,
//         &g_dispatcher_task);

//     if (ret != pdPASS) {
//         ESP_LOGE(TAG, "Failed to create dispatcher task");
//         vQueueDelete(g_event_queue);
//         g_event_queue = NULL;
//         return ESP_FAIL;
//     }

//     ESP_LOGI(TAG, "Event dispatcher initialized");
//     return ESP_OK;
// }

// esp_err_t event_dispatcher_post_event(const system_event_t *event)
// {
//     if (g_event_queue == NULL) {
//         return ESP_ERR_INVALID_STATE;
//     }
//     if (event == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     if (xQueueSend(g_event_queue, event, pdMS_TO_TICKS(100)) != pdTRUE) {
//         ESP_LOGW(TAG, "Event queue full");
//         return ESP_FAIL;
//     }

//     return ESP_OK;
// }

// esp_err_t event_dispatcher_post_event_from_isr(
//     const system_event_t *event,
//     BaseType_t *pxHigherPriorityTaskWoken)
// {
//     if (g_event_queue == NULL) {
//         return ESP_ERR_INVALID_STATE;
//     }
//     if (event == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     if (xQueueSendFromISR(g_event_queue, event,
//                           pxHigherPriorityTaskWoken) != pdTRUE) {
//         return ESP_FAIL;
//     }

//     return ESP_OK;
// }

// QueueHandle_t event_dispatcher_get_queue(void)
// {
//     return g_event_queue;
// }