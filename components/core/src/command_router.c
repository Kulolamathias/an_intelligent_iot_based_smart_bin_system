/**
 * @file command_router.c
 * @brief Command Router – Implementation
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the static command handler registry.
 * No decision logic – pure lookup and dispatch.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - Registry array size is exactly CMD_COUNT.
 * - Unused entries have .registered = false.
 * - Handler functions are never called with NULL params unless the command
 *   specification allows it.
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#include "command_router.h"
#include <stdbool.h>
#include "esp_log.h"

#define MAX_REGISTERED_COMMANDS CMD_COUNT
static const char *TAG = "CommandRouter";

/* ============================================================
 * HANDLER REGISTRY ENTRY
 * ============================================================ */
typedef struct {
    esp_err_t (*handler)(void *context, void *params); /**< Function pointer */
    void *context;                                     /**< Service-supplied context */
    bool registered;                                   /**< true if slot occupied */
} command_handler_entry_t;

/* ============================================================
 * STATIC REGISTRY – all slots initially empty
 * ============================================================ */
static command_handler_entry_t g_command_registry[MAX_REGISTERED_COMMANDS] = {0};

/* ============================================================
 * INTERNAL VALIDATION
 * ============================================================ */
static bool is_valid_command(system_command_id_t command)
{
    return (command >= 0 && command < CMD_COUNT);
}

/* ============================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================ */

esp_err_t command_router_init(void)
{
    ESP_LOGI(TAG, "Command router initialized (registry empty)");
    return ESP_OK;
}

esp_err_t command_router_execute(system_command_id_t command, void *params)
{
    if (!is_valid_command(command)) {
        ESP_LOGE(TAG, "Invalid command id: %d", command);
        return ESP_ERR_INVALID_ARG;
    }

    command_handler_entry_t *entry = &g_command_registry[command];
    if (!entry->registered) {
        ESP_LOGW(TAG, "No handler registered for command %d", command);
        return ESP_ERR_NOT_FOUND;
    }

    return entry->handler(entry->context, params);
}

esp_err_t command_router_register_handler(
    system_command_id_t command,
    esp_err_t (*handler)(void *context, void *params),
    void *context)
{
    if (!is_valid_command(command) || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    command_handler_entry_t *entry = &g_command_registry[command];
    if (entry->registered) {
        return ESP_ERR_INVALID_STATE;
    }

    entry->handler = handler;
    entry->context = context;
    entry->registered = true;

    return ESP_OK;
}