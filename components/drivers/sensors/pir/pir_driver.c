/**
 * @file components/drivers/sensors/pir/pir_driver.c
 * @brief PIR Motion Sensor Driver Implementation
 */

#include "pir_driver.h"
#include "utils_common.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PIR";

/* ============================================================
 * PRIVATE VARIABLES
 * ============================================================ */

static pir_config_t s_config = PIR_DEFAULT_CONFIG;
static QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_pir_task = NULL;
static bool s_initialized = false;
static bool s_motion_detected = false;
static uint32_t s_last_motion_time = 0;

/* ============================================================
 * PRIVATE FUNCTION DECLARATIONS
 * ============================================================ */

static void pir_isr_handler(void *arg);
static void pir_task(void *pvParameters);
static void post_event(pir_event_type_t event_type);

/* ============================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * ============================================================ */

/**
 * @brief GPIO interrupt handler for PIR sensor
 */
static void IRAM_ATTR pir_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Post event to task for processing (deferred processing)
    pir_event_t event = {
        .type = PIR_EVENT_MOTION_DETECTED,
        .timestamp_ms = time_get_ms()
    };
    
    xQueueSendFromISR(s_event_queue, &event, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief PIR background task for debouncing and timeout detection
 */
static void pir_task(void *pvParameters) {
    LOGI(TAG, "PIR task started");
    
    pir_event_t event;
    
    while (1) {
        // Wait for events
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY)) {
            switch (event.type) {
                case PIR_EVENT_MOTION_DETECTED:
                    // Debounce check
                    if (time_elapsed_ms(s_last_motion_time) > s_config.debounce_ms) {
                        s_motion_detected = true;
                        s_last_motion_time = event.timestamp_ms;
                        LOGD(TAG, "Motion detected");
                    }
                    break;
                    
                case PIR_EVENT_ERROR:
                    LOGE(TAG, "Error event received");
                    break;
                    
                default:
                    break;
            }
        }
        
        // Check for motion timeout
        if (s_motion_detected && 
            time_elapsed_ms(s_last_motion_time) > s_config.timeout_ms) {
            s_motion_detected = false;
            LOGD(TAG, "Motion ended (timeout)");
        }
    }
}

/**
 * @brief Post an event to the event queue
 */
static void post_event(pir_event_type_t event_type) {
    if (s_event_queue == NULL) return;
    
    pir_event_t event = {
        .type = event_type,
        .timestamp_ms = time_get_ms()
    };
    
    xQueueSend(s_event_queue, &event, pdMS_TO_TICKS(100));
}

/* ============================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================ */

esp_err_t pir_init(const pir_config_t *config) {
    if (s_initialized) {
        LOGW(TAG, "Driver already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&s_config, config, sizeof(pir_config_t));
    
    // Create event queue
    s_event_queue = xQueueCreate(10, sizeof(pir_event_t));
    if (s_event_queue == NULL) {
        LOGE(TAG, "Failed to create event queue");
        return ESP_FAIL;
    }
    
    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_config.gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = s_config.active_high ? 
                     GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        LOGE(TAG, "Failed to configure GPIO");
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return err;
    }
    
    // Install ISR service if not already installed
    gpio_install_isr_service(0);
    
    // Add ISR handler
    err = gpio_isr_handler_add(s_config.gpio_num, pir_isr_handler, NULL);
    if (err != ESP_OK) {
        LOGE(TAG, "Failed to add ISR handler");
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return err;
    }
    
    // Create PIR task
    BaseType_t task_result = xTaskCreate(
        pir_task,
        "pir_task",
        2048,
        NULL,
        tskIDLE_PRIORITY + 1,
        &s_pir_task
    );
    
    if (task_result != pdPASS) {
        LOGE(TAG, "Failed to create PIR task");
        gpio_isr_handler_remove(s_config.gpio_num);
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_FAIL;
    }
    
    s_initialized = true;
    s_motion_detected = false;
    s_last_motion_time = 0;
    
    LOGI(TAG, "PIR driver initialized");
    LOGI(TAG, "  GPIO: %d", s_config.gpio_num);
    LOGI(TAG, "  Debounce: %d ms", s_config.debounce_ms);
    LOGI(TAG, "  Timeout: %d ms", s_config.timeout_ms);
    LOGI(TAG, "  Active high: %s", s_config.active_high ? "yes" : "no");
    
    return ESP_OK;
}

void pir_deinit(void) {
    if (!s_initialized) {
        return;
    }
    
    // Remove ISR handler
    gpio_isr_handler_remove(s_config.gpio_num);
    
    // Delete task
    if (s_pir_task != NULL) {
        vTaskDelete(s_pir_task);
        s_pir_task = NULL;
    }
    
    // Delete event queue
    if (s_event_queue != NULL) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    
    s_initialized = false;
    s_motion_detected = false;
    
    LOGI(TAG, "PIR driver deinitialized");
}

esp_err_t pir_get_state(bool *motion_detected) {
    if (!s_initialized || motion_detected == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *motion_detected = s_motion_detected;
    return ESP_OK;
}

QueueHandle_t pir_get_event_queue(void) {
    return s_event_queue;
}

esp_err_t pir_set_timeout(uint32_t timeout_ms) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (timeout_ms < 1000 || timeout_ms > 60000) {
        LOGE(TAG, "Invalid timeout: %d ms", timeout_ms);
        return ESP_ERR_INVALID_ARG;
    }
    
    s_config.timeout_ms = timeout_ms;
    LOGI(TAG, "Timeout set to %d ms", timeout_ms);
    
    return ESP_OK;
}

esp_err_t pir_self_test(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    LOGI(TAG, "Starting self-test");
    
    // Test 1: Read current state
    bool current_state;
    esp_err_t err = pir_get_state(&current_state);
    if (err != ESP_OK) {
        LOGE(TAG, "Self-test failed: Cannot read state");
        return err;
    }
    
    LOGI(TAG, "Current motion state: %s", 
             current_state ? "detected" : "not detected");
    
    // Test 2: Trigger manual read of GPIO
    int gpio_level = gpio_get_level(s_config.gpio_num);
    LOGI(TAG, "GPIO level: %d", gpio_level);
    
    // The PIR sensor might not be detecting motion during test
    // So we just check if we can read the GPIO
    if (gpio_level < 0) {
        LOGE(TAG, "Self-test failed: Cannot read GPIO");
        return ESP_FAIL;
    }
    
    LOGI(TAG, "Self-test passed");
    return ESP_OK;
}