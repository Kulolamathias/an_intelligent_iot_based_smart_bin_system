

/**
 * @file app_main.c
 * @brief Validation harness for timer, indicator, ultrasonic, and servo services.
 *
 * =============================================================================
 * Steps:
 *   1. Core initialisation.
 *   2. Service initialisation via service_manager.
 *   3. Register handlers.
 *   4. Start services.
 *   5. Wait for stabilisation.
 *   6. Dispatch test commands (indicator, timer, ultrasonic, servo).
 *   7. Keep main task alive.
 * =============================================================================
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "command_router.h"
#include "event_dispatcher.h"
#include "service_manager.h"
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
     * 2. Service initialisation
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

    /* ---- Indicator test ---- */
    cmd_update_indicators_params_t ind_params = {
        .led_pattern = 2,
        .buzzer_pattern = 3,
        .lcd_pattern = 1
    };
    ESP_LOGI(TAG, "Dispatching CMD_UPDATE_INDICATORS");
    command_router_execute(CMD_UPDATE_INDICATORS, &ind_params);

    /* ---- Periodic timer test (5 sec) ---- */
    cmd_start_timer_params_t timer_params = { .timeout_ms = 5000 };
    ESP_LOGI(TAG, "Dispatching CMD_START_PERIODIC_TIMER (5000 ms)");
    command_router_execute(CMD_START_PERIODIC_TIMER, &timer_params);

    /* ---- Oneshot timer test (15 sec) ---- */
    timer_params.timeout_ms = 15000;
    ESP_LOGI(TAG, "Dispatching CMD_START_INTENT_TIMER (15000 ms)");
    command_router_execute(CMD_START_INTENT_TIMER, &timer_params);

    /* ---- Ultrasonic fill level test (every 5 sec) – loop later ---- */

    /* ---- Servo test sequence ---- */
    vTaskDelay(pdMS_TO_TICKS(2000));

    servo_command_data_t servo_cmd = {
        .servo_id = SERVO_LID,
        .angle_deg = 0  /* not used for open/close, but keep */
    };

    ESP_LOGI(TAG, "Dispatching CMD_OPEN_LID (lid)");
    command_router_execute(CMD_OPEN_LID, &servo_cmd);

    vTaskDelay(pdMS_TO_TICKS(5000));   /* wait for motion to complete (smooth) */

    ESP_LOGI(TAG, "Dispatching CMD_CLOSE_LID (lid)");
    command_router_execute(CMD_CLOSE_LID, &servo_cmd);

    vTaskDelay(pdMS_TO_TICKS(5000));

    /* ---- Continuous ultrasonic fill measurement ---- */
    int loop = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Dispatching CMD_READ_FILL_LEVEL (attempt %d)", ++loop);
        command_router_execute(CMD_READ_FILL_LEVEL, NULL);
    }
}



















// /**
//  * @file app_main.c
//  * @brief Validation harness for timer, indicator, and ultrasonic services.
//  *
//  * =============================================================================
//  * This file demonstrates correct boot order and direct command injection
//  * to verify service behaviour without core FSM involvement.
//  *
//  * Added ultrasonic test:
//  *   - After initialisation, a loop sends CMD_READ_FILL_LEVEL every 5 seconds.
//  *   - The ultrasonic service will measure the fill sensor, post EVENT_BIN_LEVEL_UPDATED,
//  *     and also log the distance and fill percentage at DEBUG level.
//  *   - The intent sensor runs autonomously via its own timer and posts
//  *     EVENT_CLOSE_RANGE_DETECTED/LOST when state changes.
//  *
//  * All commands are sent using command_router_execute().
//  * Parameter structures are stack‑allocated and zero‑initialised.
//  * =============================================================================
//  */

// #include <stdio.h>
// #include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_log.h"

// /* Core headers */
// #include "command_router.h"
// #include "event_dispatcher.h"

// /* Service manager */
// #include "service_manager.h"

// /* Command parameter definitions */
// #include "command_params.h"

// static const char *TAG = "APP_MAIN";

// void app_main(void)
// {
//     ESP_LOGI(TAG, "=== System Validation Harness Starting ===");

//     /* --------------------------------------------------------
//      * 1. Core initialisation
//      * -------------------------------------------------------- */
//     ESP_LOGI(TAG, "Initialising command router");
//     esp_err_t ret = command_router_init();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "command_router_init failed: %d", ret);
//         return;
//     }

//     ESP_LOGI(TAG, "Initialising event dispatcher");
//     ret = event_dispatcher_init();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret);
//         return;
//     }

//     /* --------------------------------------------------------
//      * 2. Service initialisation (all services)
//      * -------------------------------------------------------- */
//     ret = service_manager_init_all();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "service_manager_init_all failed: %d", ret);
//         return;
//     }

//     /* --------------------------------------------------------
//      * 3. Register command handlers
//      * -------------------------------------------------------- */
//     ret = service_manager_register_all();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "service_manager_register_all failed: %d", ret);
//         return;
//     }

//     /* --------------------------------------------------------
//      * 4. Start services
//      * -------------------------------------------------------- */
//     ret = service_manager_start_all();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "service_manager_start_all failed: %d", ret);
//         return;
//     }

//     /* --------------------------------------------------------
//      * 5. Allow system to stabilise
//      * -------------------------------------------------------- */
//     ESP_LOGI(TAG, "Waiting 1 second for stabilisation...");
//     vTaskDelay(pdMS_TO_TICKS(1000));

//     /* --------------------------------------------------------
//      * 6. Dispatch test commands (timer & indicator)
//      * -------------------------------------------------------- */

//     /* ---- Indicator test: LED slow blink, buzzer fast blink, LCD solid ---- */
//     cmd_update_indicators_params_t ind_params;
//     memset(&ind_params, 0, sizeof(ind_params));
//     ind_params.led_pattern = 2;      /* slow blink (500 ms) */
//     ind_params.buzzer_pattern = 3;   /* fast blink (200 ms) */
//     ind_params.lcd_pattern = 1;      /* solid on */

//     ESP_LOGI(TAG, "Dispatching CMD_UPDATE_INDICATORS");
//     ret = command_router_execute(CMD_UPDATE_INDICATORS, &ind_params);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "CMD_UPDATE_INDICATORS failed: %d", ret);
//     }

//     /* ---- Periodic timer test: 5000 ms ---- */
//     cmd_start_timer_params_t timer_params;
//     memset(&timer_params, 0, sizeof(timer_params));
//     timer_params.timeout_ms = 5000;

//     ESP_LOGI(TAG, "Dispatching CMD_START_PERIODIC_TIMER (5000 ms)");
//     ret = command_router_execute(CMD_START_PERIODIC_TIMER, &timer_params);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "CMD_START_PERIODIC_TIMER failed: %d", ret);
//     }

//     /* ---- Oneshot timer test: 15000 ms (using CMD_START_INTENT_TIMER) ---- */
//     timer_params.timeout_ms = 15000;
//     ESP_LOGI(TAG, "Dispatching CMD_START_INTENT_TIMER (15000 ms)");
//     ret = command_router_execute(CMD_START_INTENT_TIMER, &timer_params);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "CMD_START_INTENT_TIMER failed: %d", ret);
//     }

//     /* --------------------------------------------------------
//      * 7. Ultrasonic test – periodic fill level measurement
//      * -------------------------------------------------------- */
//     ESP_LOGI(TAG, "Starting ultrasonic fill level test (every 5s)");
//     int loop_count = 0;
//     while (1) {
//         vTaskDelay(pdMS_TO_TICKS(5000));   /* wait 5 seconds between measurements */

//         ESP_LOGI(TAG, "Dispatching CMD_READ_FILL_LEVEL (attempt %d)", ++loop_count);
//         ret = command_router_execute(CMD_READ_FILL_LEVEL, NULL);
//         if (ret != ESP_OK) {
//             ESP_LOGE(TAG, "CMD_READ_FILL_LEVEL failed: %d", ret);
//         } else {
//             ESP_LOGI(TAG, "CMD_READ_FILL_LEVEL dispatched successfully – check DEBUG logs for distance/percentage");
//         }

//         /* The intent sensor runs autonomously; its events will appear in logs
//          * if DEBUG level is enabled for the ULTRASONIC_SVC tag.
//          */
//     }

//     /* Never reached – infinite loop above */
// }