/**
 * @file ultrasonic_driver.h
 * @brief Ultrasonic sensor driver – instance-based hardware abstraction.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle-based interface to individual ultrasonic
 * sensors. It manages trigger/echo GPIO pins, generates the 10µs trigger
 * pulse, and measures the echo pulse width using esp_timer.
 *
 * It contains NO business logic, NO filtering, and NO event dispatch.
 * =============================================================================
 */

#ifndef ULTRASONIC_DRIVER_H
#define ULTRASONIC_DRIVER_H

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle representing an ultrasonic sensor instance.
 */
typedef struct ultrasonic_handle_t *ultrasonic_handle_t;

/**
 * @brief Configuration for creating an ultrasonic sensor instance.
 */
typedef struct {
    gpio_num_t trig_pin;      /**< Trigger GPIO (output) */
    gpio_num_t echo_pin;       /**< Echo GPIO (input, with interrupt?) */
    uint32_t timeout_us;       /**< Max echo wait time in microseconds */
} ultrasonic_config_t;

/**
 * @brief Create an ultrasonic sensor instance.
 *
 * @param cfg       Configuration (pins, timeout).
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t ultrasonic_driver_create(const ultrasonic_config_t *cfg,
                                   ultrasonic_handle_t *out_handle);

/**
 * @brief Perform a single measurement.
 *
 * This function blocks for the duration of the measurement (up to timeout_us).
 * It triggers the sensor and measures the echo pulse width.
 *
 * @param handle         Instance handle.
 * @param pulse_width_us Pointer to store measured pulse width in microseconds.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no echo, other error on failure.
 */
esp_err_t ultrasonic_driver_measure(ultrasonic_handle_t handle,
                                    uint32_t *pulse_width_us);

/**
 * @brief Delete an ultrasonic sensor instance and free resources.
 *
 * @param handle Instance handle.
 * @return ESP_OK on success.
 */
esp_err_t ultrasonic_driver_delete(ultrasonic_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* ULTRASONIC_DRIVER_H */