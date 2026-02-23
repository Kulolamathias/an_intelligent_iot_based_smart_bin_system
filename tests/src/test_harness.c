/**
 * @file tests/src/test_harness.c
 * @brief Test harness implementation.
 * @author Mathias Kulola
 * @date 2024-12-23
 * @version 1.0.0
 */

#include "test_harness.h"
#include "event_dispatcher.h"
#include "state_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "TestHarness";

// Test statistics
static uint32_t g_test_event_count = 0;
static uint32_t g_test_start_time = 0;
static test_result_t g_last_result = {0};

static void delayed_event_task(void *pvParameters)
{
    uint32_t delay_ms = (uint32_t)pvParameters & 0xFFFF;
    system_event_id_t event_id = (system_event_id_t)((uint32_t)pvParameters >> 16);
    
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    
    system_event_t event = {
        .event_id = event_id,
        .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS,
        .data = NULL
    };
    
    event_dispatcher_post_event(event);
    g_test_event_count++;
    
    ESP_LOGI(TAG, "Delayed event posted: %d after %lums", event_id, delay_ms);
    vTaskDelete(NULL);
}

esp_err_t test_harness_init(void)
{
    ESP_LOGI(TAG, "Initializing Test Harness");
    return ESP_OK;
}

static esp_err_t run_basic_operation_test(test_result_t *result)
{
    ESP_LOGI(TAG, "=== Running Basic Operation Test ===");
    strcpy(result->description, "Basic waste disposal flow");
    
    g_test_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_test_event_count = 0;
    
    // Sequence: Person detected -> Intent confirmed -> Lid opens -> Person leaves -> Lid closes
    test_harness_post_event_delayed(EVENT_PERSON_DETECTED, 1000);
    test_harness_post_event_delayed(EVENT_INTENT_CONFIRMED, 2000);
    test_harness_post_event_delayed(EVENT_PERSON_LEFT, 5000);
    
    // Wait for sequence to complete
    vTaskDelay(pdMS_TO_TICKS(8000));
    
    // Verify final state
    system_state_t final_state = state_manager_get_current_state();
    bool test_passed = (final_state == SYSTEM_STATE_IDLE);
    
    // Fill results
    result->passed = test_passed;
    result->duration_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) - g_test_start_time;
    result->events_processed = g_test_event_count;
    result->state_transitions = 0; // Would need to track this
    result->errors_encountered = 0;
    
    ESP_LOGI(TAG, "Basic Operation Test %s", test_passed ? "PASSED" : "FAILED");
    ESP_LOGI(TAG, "Final State: %d (Expected: %d)", final_state, SYSTEM_STATE_IDLE);
    
    return test_passed ? ESP_OK : ESP_FAIL;
}

static esp_err_t run_fill_cycle_test(test_result_t *result)
{
    ESP_LOGI(TAG, "=== Running Fill Cycle Test ===");
    strcpy(result->description, "Bin filling from empty to full");
    
    g_test_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_test_event_count = 0;
    
    // Simulate multiple usage cycles until bin fills
    for (int i = 0; i < 5; i++) {
        test_harness_post_event_delayed(EVENT_PERSON_DETECTED, 1000 + i*2000);
        test_harness_post_event_delayed(EVENT_INTENT_CONFIRMED, 1500 + i*2000);
        test_harness_post_event_delayed(EVENT_PERSON_LEFT, 3000 + i*2000);
        test_harness_post_event_delayed(EVENT_LID_CLOSED, 3500 + i*2000);
        
        // Simulate fill level increase
        uint8_t fill_level = 20 + i*15;
        uint8_t *level_data = malloc(sizeof(uint8_t));
        if (level_data) {
            *level_data = fill_level;
            test_harness_post_event_delayed(EVENT_BIN_LEVEL_UPDATED, 4000 + i*2000);
            free(level_data);
        }
    }
    
    // Post near-full and full events
    test_harness_post_event_delayed(EVENT_BIN_NEAR_FULL, 12000);
    test_harness_post_event_delayed(EVENT_BIN_FULL, 15000);
    
    vTaskDelay(pdMS_TO_TICKS(20000));
    
    system_state_t final_state = state_manager_get_current_state();
    bool test_passed = (final_state == SYSTEM_STATE_FULL);
    
    result->passed = test_passed;
    result->duration_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) - g_test_start_time;
    result->events_processed = g_test_event_count;
    
    ESP_LOGI(TAG, "Fill Cycle Test %s", test_passed ? "PASSED" : "FAILED");
    return test_passed ? ESP_OK : ESP_FAIL;
}

esp_err_t test_harness_run_scenario(test_scenario_t scenario, test_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    
    memset(result, 0, sizeof(test_result_t));
    
    switch (scenario) {
        case TEST_SCENARIO_BASIC_OPERATION:
            return run_basic_operation_test(result);
            
        case TEST_SCENARIO_FILL_CYCLE:
            return run_fill_cycle_test(result);
            
        case TEST_SCENARIO_MAINTENANCE:
            // TODO: Implement
            strcpy(result->description, "Maintenance scenario (TODO)");
            result->passed = true;
            return ESP_OK;
            
        case TEST_SCENARIO_ERROR_RECOVERY:
            // TODO: Implement
            strcpy(result->description, "Error recovery scenario (TODO)");
            result->passed = true;
            return ESP_OK;
            
        default:
            ESP_LOGE(TAG, "Unknown test scenario: %d", scenario);
            return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t test_harness_run_all(void)
{
    ESP_LOGI(TAG, "=== Running All Test Scenarios ===");
    
    test_result_t results[6];
    bool all_passed = true;
    
    for (int i = 0; i < 2; i++) { // Only run implemented tests for now
        esp_err_t ret = test_harness_run_scenario(i, &results[i]);
        if (ret != ESP_OK || !results[i].passed) {
            all_passed = false;
        }
        
        // Store last result for report
        memcpy(&g_last_result, &results[i], sizeof(test_result_t));
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay between tests
    }
    
    test_harness_generate_report();
    
    return all_passed ? ESP_OK : ESP_FAIL;
}

esp_err_t test_harness_post_event_delayed(system_event_id_t event_id, uint32_t delay_ms)
{
    // Create parameter combining event_id and delay
    uint32_t param = ((uint32_t)event_id << 16) | (delay_ms & 0xFFFF);
    
    TaskHandle_t task_handle;
    BaseType_t ret = xTaskCreate(
        delayed_event_task,
        "DelayedEvent",
        2048,
        (void*)param,
        tskIDLE_PRIORITY,
        &task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create delayed event task");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

void test_harness_interactive_mode(void)
{
    ESP_LOGI(TAG, "=== Interactive Test Mode ===");
    ESP_LOGI(TAG, "Available commands:");
    ESP_LOGI(TAG, "  0: EVENT_PERSON_DETECTED");
    ESP_LOGI(TAG, "  1: EVENT_PERSON_LEFT");
    ESP_LOGI(TAG, "  2: EVENT_INTENT_CONFIRMED");
    ESP_LOGI(TAG, "  3: EVENT_INTENT_TIMEOUT");
    ESP_LOGI(TAG, "  4: EVENT_BIN_LEVEL_UPDATED");
    ESP_LOGI(TAG, "  5: EVENT_BIN_NEAR_FULL");
    ESP_LOGI(TAG, "  6: EVENT_BIN_FULL");
    ESP_LOGI(TAG, "  7: EVENT_BIN_EMPTIED");
    ESP_LOGI(TAG, "  8: EVENT_LID_OPENED");
    ESP_LOGI(TAG, "  9: EVENT_LID_CLOSED");
    ESP_LOGI(TAG, "  w: EVENT_WIFI_CONNECTED");
    ESP_LOGI(TAG, "  d: EVENT_WIFI_DISCONNECTED");
    ESP_LOGI(TAG, "  s: Show current state");
    ESP_LOGI(TAG, "  q: Quit interactive mode");
    
    while (1) {
        ESP_LOGI(TAG, "Enter command: ");
        
        // Note: In real implementation, read from serial input
        // For now, we'll simulate with a simple loop
        vTaskDelay(pdMS_TO_TICKS(10000));
        break; // Exit after 10 seconds for demo
    }
}

void test_harness_generate_report(void)
{
    ESP_LOGI(TAG, "=== Test Report ===");
    ESP_LOGI(TAG, "Test: %s", g_last_result.description);
    ESP_LOGI(TAG, "Result: %s", g_last_result.passed ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "Duration: %lums", g_last_result.duration_ms);
    ESP_LOGI(TAG, "Events Processed: %lu", g_last_result.events_processed);
    ESP_LOGI(TAG, "State Transitions: %lu", g_last_result.state_transitions);
    ESP_LOGI(TAG, "Errors: %lu", g_last_result.errors_encountered);
    ESP_LOGI(TAG, "Current State: %d", state_manager_get_current_state());
    
    const system_context_t *ctx = state_manager_get_context();
    ESP_LOGI(TAG, "Fill Level: %d%%", ctx->bin_fill_level_percent);
    ESP_LOGI(TAG, "Bin Locked: %s", ctx->bin_locked ? "YES" : "NO");
    ESP_LOGI(TAG, "WiFi Connected: %s", ctx->wifi_connected ? "YES" : "NO");
}