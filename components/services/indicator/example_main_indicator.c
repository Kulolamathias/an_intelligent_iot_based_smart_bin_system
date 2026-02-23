/**
 * @file app_main.c
 * @brief Temporary validation harness for timer and indicator services.
 *
 * =============================================================================
 * This file demonstrates correct boot order and direct command injection
 * to verify service behaviour without core FSM involvement.
 *
 * Steps:
 *   1. Initialise core: command_router, event_dispatcher.
 *   2. Initialise all services via service_manager.
 *   3. Register all command handlers.
 *   4. Start all services.
 *   5. Wait 1 second for system stabilisation.
 *   6. Dispatch test commands to indicator and timer services.
 *   7. Keep main task alive with infinite loop (vTaskDelay) to observe.
 *
 * All commands are sent using command_router_execute().
 * Parameter structures are stack‑allocated and zero‑initialised.
 * =============================================================================
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* Core headers */
#include "command_router.h"
#include "event_dispatcher.h"

/* Service manager */
#include "service_manager.h"

/* Command parameter definitions */
#include "command_params.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=== System Validation Harness Starting ===");

    /* --------------------------------------------------------
     * 1. Core initialisation
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "Initialising command router");
    esp_err_t ret = command_router_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "command_router_init failed: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "Initialising event dispatcher");
    ret = event_dispatcher_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------
     * 2. Service initialisation (all services)
     * -------------------------------------------------------- */
    ret = service_manager_init_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_init_all failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------
     * 3. Register command handlers
     * -------------------------------------------------------- */
    ret = service_manager_register_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_register_all failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------
     * 4. Start services
     * -------------------------------------------------------- */
    ret = service_manager_start_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_start_all failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------
     * 5. Allow system to stabilise
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "Waiting 1 second for stabilisation...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* --------------------------------------------------------
     * 6. Dispatch test commands
     * -------------------------------------------------------- */

    /* ---- Indicator test: LED slow blink, buzzer fast blink, LCD solid ---- */
    cmd_update_indicators_params_t ind_params;
    memset(&ind_params, 0, sizeof(ind_params));
    ind_params.led_pattern = 2;      /* slow blink (500 ms) */
    ind_params.buzzer_pattern = 3;   /* fast blink (200 ms) */
    ind_params.lcd_pattern = 1;      /* solid on */

    ESP_LOGI(TAG, "Dispatching CMD_UPDATE_INDICATORS");
    ret = command_router_execute(CMD_UPDATE_INDICATORS, &ind_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CMD_UPDATE_INDICATORS failed: %d", ret);
    }

    /* ---- Periodic timer test: 5000 ms ---- */
    cmd_start_timer_params_t timer_params;
    memset(&timer_params, 0, sizeof(timer_params));
    timer_params.timeout_ms = 5000;

    ESP_LOGI(TAG, "Dispatching CMD_START_PERIODIC_TIMER (5000 ms)");
    ret = command_router_execute(CMD_START_PERIODIC_TIMER, &timer_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CMD_START_PERIODIC_TIMER failed: %d", ret);
    }

    /* ---- Oneshot timer test: 15000 ms (using CMD_START_INTENT_TIMER) ---- */
    timer_params.timeout_ms = 15000;
    ESP_LOGI(TAG, "Dispatching CMD_START_INTENT_TIMER (15000 ms)");
    ret = command_router_execute(CMD_START_INTENT_TIMER, &timer_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CMD_START_INTENT_TIMER failed: %d", ret);
    }

    /* --------------------------------------------------------
     * 7. Keep main task alive for observation
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "Validation harness running. Main task suspended.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}