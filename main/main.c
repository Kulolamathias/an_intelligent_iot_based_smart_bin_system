/**
 * @file main.c
 * @brief Test entry – runs selected tests with full service initialisation.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This main file initialises the core, services, network stack, and NVS,
 * then runs a test suite. It follows the same pattern as the rocket project,
 * ensuring deterministic startup order.
 *
 * =============================================================================
 * LIFECYCLE
 * =============================================================================
 * 1. command_router_init()
 * 2. event_dispatcher_init()   (only creates queue, no task yet)
 * 3. nvs_flash_init()
 * 4. esp_netif_init() + esp_event_loop_create_default()
 * 5. service_manager_init_all()
 * 6. service_manager_register_all()
 * 7. command_router_lock()      <-- added
 * 8. service_manager_start_all()
 * 9. event_dispatcher_start()   (now dispatcher task is safe)
 * 10. Run tests
 * 11. Idle loop
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-03-29
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "command_router.h"
#include "event_dispatcher.h"
#include "service_manager.h"

#include "command_params.h"

#include "mqtt_topic.h"

/* Test headers */
#include "ultrasonic_test.h"
#include "wifi_mqtt_test.h"

static const char *TAG = "MAIN";

static struct {
    const char *name;
    esp_err_t (*run)(void);
} s_tests[] = {
    // { "ultrasonic",  ultrasonic_test_run },
    // { "wifi_mqtt",   wifi_mqtt_test_run  },
};

#define TEST_COUNT (sizeof(s_tests) / sizeof(s_tests[0]))

void app_main(void)
{
    esp_err_t ret;

    /* 1. Core initialisation (only registry, no tasks) */
    command_router_init();

    ret = event_dispatcher_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret);
        return;
    }

    /* 2. NVS initialisation (required for WiFi) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 3. TCP/IP stack and default event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 4. Service lifecycle: init → register → start */
    ret = service_manager_init_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_init_all failed: %d", ret);
        return;
    }

    ret = service_manager_register_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_register_all failed: %d", ret);
        return;
    }

    /* Lock the command router – after this no more handlers can be registered */
    command_router_lock();
    vTaskDelay(pdMS_TO_TICKS(50));  /* brief delay to ensure all handlers are registered before starting services */

    /* 5. Start event dispatcher (task now created) */
    ret = event_dispatcher_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_start failed: %d", ret);
        return;
    }

    ret = service_manager_start_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_start_all failed: %d", ret);
        return;
    }

    /* Let system settle */
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "System ready. Running tests...");

    /* 6. Run all tests sequentially */
    for (size_t i = 0; i < TEST_COUNT; i++) {
        ESP_LOGI(TAG, "=== Running test: %s ===", s_tests[i].name);
        ret = s_tests[i].run();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Test %s failed: %d", s_tests[i].name, ret);
        } else {
            ESP_LOGI(TAG, "Test %s passed.", s_tests[i].name);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "All tests completed. System idling.");


    vTaskDelete(NULL); /* Delete main task .. we don't need it anymore */

    /* 7. Idle loop – keep the system alive */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
