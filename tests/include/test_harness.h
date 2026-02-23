/**
 * @file tests/include/test_harness.h
 * @brief Professional test harness for Smart Bin System.
 * @author Mathias Kulola
 * @date 2024-12-23
 * @version 1.0.0
 */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include "core_types.h"
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Test scenario identifiers.
 */
typedef enum {
    TEST_SCENARIO_BASIC_OPERATION = 0,  /**< Normal waste disposal flow */
    TEST_SCENARIO_FILL_CYCLE,           /**< Bin filling to full */
    TEST_SCENARIO_MAINTENANCE,          /**< Maintenance/emptying flow */
    TEST_SCENARIO_ERROR_RECOVERY,       /**< Error handling and recovery */
    TEST_SCENARIO_STRESS_TEST,          /**< Stress test with rapid events */
    TEST_SCENARIO_NETWORK_FLUCTUATION,  /**< Network connection changes */
    TEST_SCENARIO_CUSTOM                /**< User-defined scenario */
} test_scenario_t;

/**
 * @brief Test result structure.
 */
typedef struct {
    bool passed;
    uint32_t duration_ms;
    uint32_t events_processed;
    uint32_t state_transitions;
    uint32_t errors_encountered;
    char description[256];
} test_result_t;

/**
 * @brief Initialize the test harness.
 * @return ESP_OK on success.
 */
esp_err_t test_harness_init(void);

/**
 * @brief Run a specific test scenario.
 * @param scenario The test scenario to run.
 * @param result[out] Pointer to store test results.
 * @return ESP_OK if test completed.
 */
esp_err_t test_harness_run_scenario(test_scenario_t scenario, test_result_t *result);

/**
 * @brief Run all test scenarios.
 * @return ESP_OK if all tests passed.
 */
esp_err_t test_harness_run_all(void);

/**
 * @brief Create and post a test event.
 * @param event_id Event to post.
 * @param delay_ms Delay before posting (simulates async).
 * @return ESP_OK on success.
 */
esp_err_t test_harness_post_event_delayed(system_event_id_t event_id, uint32_t delay_ms);

/**
 * @brief Run interactive test mode (manual event triggering).
 */
void test_harness_interactive_mode(void);

/**
 * @brief Generate test report.
 */
void test_harness_generate_report(void);

#endif /* TEST_HARNESS_H */