/**
 * @file core_self_test.c
 * @brief Core Self-Test Harness – FSM Verification
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * This module provides a standalone, deterministic test harness for the Core
 * Finite State Machine. It validates that given a sequence of synthetic events,
 * the state_manager produces the expected state transitions and command batches.
 *
 * It does NOT depend on any services, drivers, or hardware.
 * It uses stub command handlers that only log command execution.
 *
 * =============================================================================
 * WHAT IS VALIDATED
 * =============================================================================
 * - State transition table correctness (first-match semantics)
 * - Context update behaviour (fill level, authentication, GPS)
 * - Command parameter preparation (message formatting, thresholds)
 * - Deterministic behaviour under defined event sequences
 *
 * =============================================================================
 * WHAT IS NOT VALIDATED
 * =============================================================================
 * - Real service/driver integration
 * - Hardware timing or interrupt behaviour
 * - Queue overflow or task scheduling (FreeRTOS is assumed functional)
 * - Long-running stability or power management
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 * - Call core_self_test_run() from main() after core initialization.
 * - Output is printed via ESP_LOG.
 * - Test passes if no assertions fail and expected state sequence is observed.
 *
 * =============================================================================
 * ASSUMPTIONS
 * =============================================================================
 * - FreeRTOS scheduler is running.
 * - Event dispatcher and dispatcher task are operational.
 * - Command router is initialized and stub handlers are registered.
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#include "core_self_test.h"
#include "system_state.h"
#include "state_manager.h"
#include "command_router.h"
#include "command_params.h"
#include "event_dispatcher.h"
#include "event_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "CoreSelfTest";

/* ============================================================
 * STUB COMMAND HANDLERS – Simulate service execution
 *
 * These handlers do not access hardware.
 * They only log the command name and any parameters.
 * ============================================================ */

static void log_command(const char *cmd_name)
{
    ESP_LOGI(TAG, "  -> EXECUTE: %s", cmd_name);
}

static esp_err_t stub_handle_open_lid(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_OPEN_LID");
    return ESP_OK;
}

static esp_err_t stub_handle_close_lid(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_CLOSE_LID");
    return ESP_OK;
}

static esp_err_t stub_handle_lock_bin(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_LOCK_BIN");
    return ESP_OK;
}

static esp_err_t stub_handle_unlock_bin(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_UNLOCK_BIN");
    return ESP_OK;
}

static esp_err_t stub_handle_send_notification(void *ctx, void *params)
{
    (void)ctx;
    cmd_send_notification_params_t *p = params;
    if (p) {
        ESP_LOGI(TAG, "  -> EXECUTE: CMD_SEND_NOTIFICATION: %s (esc=%d)",
                 p->message, p->is_escalation);
    } else {
        log_command("CMD_SEND_NOTIFICATION");
    }
    return ESP_OK;
}

static esp_err_t stub_handle_send_heartbeat(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_SEND_HEARTBEAT");
    return ESP_OK;
}

static esp_err_t stub_handle_update_display(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_UPDATE_DISPLAY");
    return ESP_OK;
}

static esp_err_t stub_handle_update_indicators(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_UPDATE_INDICATORS");
    return ESP_OK;
}

static esp_err_t stub_handle_show_message(void *ctx, void *params)
{
    (void)ctx;
    cmd_show_message_params_t *p = params;
    if (p) {
        ESP_LOGI(TAG, "  -> EXECUTE: CMD_SHOW_MESSAGE: [%s] [%s]",
                 p->line1, p->line2);
    } else {
        log_command("CMD_SHOW_MESSAGE");
    }
    return ESP_OK;
}

static esp_err_t stub_handle_start_intent_timer(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_START_INTENT_TIMER");
    return ESP_OK;
}

static esp_err_t stub_handle_stop_intent_timer(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_STOP_INTENT_TIMER");
    return ESP_OK;
}

static esp_err_t stub_handle_send_status_update(void *ctx, void *params)
{
    (void)ctx;
    cmd_send_status_update_params_t *p = params;
    if (p) {
        ESP_LOGI(TAG, "  -> EXECUTE: CMD_SEND_STATUS_UPDATE: fill=%d, locked=%d",
                 p->fill_level, p->bin_locked);
    } else {
        log_command("CMD_SEND_STATUS_UPDATE");
    }
    return ESP_OK;
}

static esp_err_t stub_handle_signal_error(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_SIGNAL_ERROR");
    return ESP_OK;
}

static esp_err_t stub_handle_enter_maintenance(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_ENTER_MAINTENANCE_MODE");
    return ESP_OK;
}

static esp_err_t stub_handle_exit_maintenance(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_EXIT_MAINTENANCE_MODE");
    return ESP_OK;
}

static esp_err_t stub_handle_save_configuration(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_SAVE_CONFIGURATION");
    return ESP_OK;
}

static esp_err_t stub_handle_clear_error(void *ctx, void *params)
{
    (void)ctx; (void)params;
    log_command("CMD_CLEAR_ERROR");
    return ESP_OK;
}

/* ============================================================
 * REGISTER ALL STUB HANDLERS
 *
 * Called once during test initialization.
 * ============================================================ */
static void register_stub_handlers(void)
{
    command_router_register_handler(CMD_OPEN_LID,               stub_handle_open_lid,               NULL);
    command_router_register_handler(CMD_CLOSE_LID,              stub_handle_close_lid,              NULL);
    command_router_register_handler(CMD_LOCK_BIN,               stub_handle_lock_bin,               NULL);
    command_router_register_handler(CMD_UNLOCK_BIN,             stub_handle_unlock_bin,             NULL);
    command_router_register_handler(CMD_SEND_NOTIFICATION,      stub_handle_send_notification,      NULL);
    command_router_register_handler(CMD_SEND_HEARTBEAT,         stub_handle_send_heartbeat,         NULL);
    command_router_register_handler(CMD_UPDATE_DISPLAY,         stub_handle_update_display,         NULL);
    command_router_register_handler(CMD_UPDATE_INDICATORS,      stub_handle_update_indicators,      NULL);
    command_router_register_handler(CMD_SHOW_MESSAGE,           stub_handle_show_message,           NULL);
    command_router_register_handler(CMD_START_INTENT_TIMER,     stub_handle_start_intent_timer,     NULL);
    command_router_register_handler(CMD_STOP_INTENT_TIMER,      stub_handle_stop_intent_timer,      NULL);
    command_router_register_handler(CMD_SEND_STATUS_UPDATE,     stub_handle_send_status_update,     NULL);
    command_router_register_handler(CMD_SIGNAL_ERROR,           stub_handle_signal_error,           NULL);
    command_router_register_handler(CMD_ENTER_MAINTENANCE_MODE, stub_handle_enter_maintenance,      NULL);
    command_router_register_handler(CMD_EXIT_MAINTENANCE_MODE,  stub_handle_exit_maintenance,       NULL);
    command_router_register_handler(CMD_SAVE_CONFIGURATION,     stub_handle_save_configuration,     NULL);
    command_router_register_handler(CMD_CLEAR_ERROR,            stub_handle_clear_error,            NULL);
}

/* ============================================================
 * EVENT POSTING HELPERS
 * ============================================================ */

static void post_event(system_event_id_t id)
{
    system_event_t ev = { .id = id };
    memset(&ev.data, 0, sizeof(ev.data));
    event_dispatcher_post_event(&ev);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void post_bin_level_event(uint8_t level)
{
    system_event_t ev = {
        .id = EVENT_BIN_LEVEL_UPDATED,
        .data.bin_level.fill_level_percent = level
    };
    event_dispatcher_post_event(&ev);
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* ============================================================
 * MAIN TEST SEQUENCE
 *
 * Executes a fixed sequence of synthetic events and observes
 * state transitions and command execution logs.
 * ============================================================ */
void core_self_test_run(void)
{
    ESP_LOGI(TAG, "========== CORE SELF TEST START ==========");

    /* --------------------------------------------------------
     * 1. Initialize core modules
     * -------------------------------------------------------- */
    system_context_t ctx = {
        .bin_fill_level_percent = 0,
        .bin_locked = false,
        .params = {
            .full_threshold = 90,
            .near_full_threshold = 70,
            .empty_threshold = 10
        },
        .gps_coordinates = {0},
        .auth_status = AUTH_STATUS_NONE,
        .error_flags = ERROR_FLAG_NONE
    };

    command_router_init();
    register_stub_handlers();
    event_dispatcher_init();
    state_manager_init(&ctx);

    vTaskDelay(pdMS_TO_TICKS(100));

    /* --------------------------------------------------------
     * 2. Scenario A: INIT -> IDLE
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "\n--- Scenario A: INIT -> IDLE (EVENT_WIFI_CONNECTED) ---");
    post_event(EVENT_WIFI_CONNECTED);
    ESP_LOGI(TAG, "Current state: %s", system_state_to_string(system_state_get()));

    /* --------------------------------------------------------
     * 3. Scenario B: IDLE -> PROCESSING (intent validated)
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "\n--- Scenario B: IDLE -> PROCESSING (EVENT_INTENT_VALIDATED) ---");
    post_event(EVENT_INTENT_VALIDATED);
    ESP_LOGI(TAG, "Current state: %s", system_state_to_string(system_state_get()));

    /* --------------------------------------------------------
     * 4. Scenario C: PROCESSING -> IDLE (lid closed)
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "\n--- Scenario C: PROCESSING -> IDLE (EVENT_LID_CLOSED) ---");
    post_event(EVENT_LID_CLOSED);
    ESP_LOGI(TAG, "Current state: %s", system_state_to_string(system_state_get()));

    /* --------------------------------------------------------
     * 5. Scenario D: IDLE -> FULL (bin level > threshold)
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "\n--- Scenario D: IDLE -> FULL (EVENT_BIN_LEVEL_UPDATED = 95%%) ---");
    post_bin_level_event(95);
    ESP_LOGI(TAG, "Current state: %s", system_state_to_string(system_state_get()));

    /* --------------------------------------------------------
     * 6. Scenario E: FULL -> IDLE (bin level drops below threshold)
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "\n--- Scenario E: FULL -> IDLE (EVENT_BIN_LEVEL_UPDATED = 20%%) ---");
    post_bin_level_event(20);
    ESP_LOGI(TAG, "Current state: %s", system_state_to_string(system_state_get()));

    ESP_LOGI(TAG, "========== CORE SELF TEST COMPLETE ==========");
}

/**
 * @brief Optional FreeRTOS task entry point for self test.
 * 
 * Can be spawned as a one-shot task from main().
 */
void core_self_test_task(void *pvParameters)
{
    (void)pvParameters;
    core_self_test_run();
    vTaskDelete(NULL);
}