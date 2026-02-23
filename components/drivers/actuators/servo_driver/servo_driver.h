/**
 * @file servo_driver.h
 * @brief Servo motor driver – instance-based PWM abstraction.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle-based interface to individual servo motors
 * using the ESP‑IDF LEDC PWM peripheral.
 *
 * It contains NO motion logic, NO sequences, and NO business policy.
 * =============================================================================
 */

#ifndef SERVO_DRIVER_H
#define SERVO_DRIVER_H

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle representing a servo instance.
 */
typedef struct servo_handle_t *servo_handle_t;

/**
 * @brief Configuration for creating a servo instance.
 */
typedef struct {
    gpio_num_t pwm_pin;               /**< GPIO connected to servo signal */
    ledc_timer_t timer;                /**< LEDC timer ID (e.g., LEDC_TIMER_0) */
    ledc_channel_t channel;            /**< LEDC channel ID (e.g., LEDC_CHANNEL_0) */
    uint32_t pwm_frequency_hz;         /**< Typically 50 Hz for standard servos */
    uint32_t min_pulse_width_us;       /**< Pulse width corresponding to 0° (e.g., 500) */
    uint32_t max_pulse_width_us;       /**< Pulse width corresponding to 180° (e.g., 2500) */
} servo_config_t;

/**
 * @brief Create a servo instance.
 *
 * @param config      Configuration structure.
 * @param out_handle  Pointer to store the created handle.
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t servo_driver_create(const servo_config_t *config,
                              servo_handle_t *out_handle);

/**
 * @brief Set servo angle (0–180 degrees).
 *
 * @param handle     Instance handle.
 * @param angle_deg  Desired angle (clamped to [0, 180]).
 * @return ESP_OK on success.
 */
esp_err_t servo_driver_set_angle(servo_handle_t handle,
                                  float angle_deg);

/**
 * @brief Stop PWM output (servo enters high‑impedance / hold?).
 *        Some servos may coast; others hold last position.
 *        This function disables the PWM signal.
 *
 * @param handle Instance handle.
 * @return ESP_OK on success.
 */
esp_err_t servo_driver_stop(servo_handle_t handle);

/**
 * @brief Delete a servo instance and free resources.
 *
 * @param handle Instance handle.
 * @return ESP_OK on success.
 */
esp_err_t servo_driver_delete(servo_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_DRIVER_H */