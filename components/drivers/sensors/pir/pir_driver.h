/**
 * @file components/drivers/sensors/pir/pir_driver.h
 * @brief PIR Motion Sensor Driver (HC-SR501)
 * 
 * DESIGN NOTES:
 * 1. Implements debouncing to avoid false triggers
 * 2. Configurable sensitivity and timeouts
 * 3. Thread-safe with event queue for async notifications
 */

#ifndef PIR_DRIVER_H
#define PIR_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ============================================================
 * CONFIGURATION
 * ============================================================ */

/**
 * @brief PIR sensor configuration
 */
typedef struct {
    int gpio_num;           /**< GPIO pin connected to PIR output */
    uint32_t debounce_ms;   /**< Debounce time in milliseconds (default: 100) */
    uint32_t timeout_ms;    /**< Timeout for motion to be considered "left" (default: 5000) */
    bool active_high;       /**< True if sensor outputs HIGH on motion (default: true) */
} pir_config_t;

/**
 * @brief Default configuration values
 */
#define PIR_DEFAULT_CONFIG { \
    .gpio_num = 4,           \
    .debounce_ms = 100,      \
    .timeout_ms = 5000,      \
    .active_high = true      \
}

/* ============================================================
 * EVENT TYPES
 * ============================================================ */

/**
 * @brief PIR detection events
 */
typedef enum {
    PIR_EVENT_MOTION_DETECTED = 0,
    PIR_EVENT_MOTION_ENDED,
    PIR_EVENT_ERROR
} pir_event_type_t;

/**
 * @brief PIR event structure
 */
typedef struct {
    pir_event_type_t type;
    uint32_t timestamp_ms;
} pir_event_t;

/* ============================================================
 * PUBLIC API
 * ============================================================ */

/**
 * @brief Initialize the PIR sensor driver
 * @param config Pointer to configuration structure
 * @return ESP_OK on success
 */
esp_err_t pir_init(const pir_config_t *config);

/**
 * @brief Deinitialize the PIR sensor driver
 */
void pir_deinit(void);

/**
 * @brief Get the current motion detection state
 * @param[out] motion_detected True if motion is currently detected
 * @return ESP_OK on success
 */
esp_err_t pir_get_state(bool *motion_detected);

/**
 * @brief Get the event queue for async notifications
 * @return Handle to the event queue (NULL if not initialized)
 */
QueueHandle_t pir_get_event_queue(void);

/**
 * @brief Set the detection timeout
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t pir_set_timeout(uint32_t timeout_ms);

/**
 * @brief Perform self-test of the PIR sensor
 * @return ESP_OK if self-test passes
 */
esp_err_t pir_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* PIR_DRIVER_H */