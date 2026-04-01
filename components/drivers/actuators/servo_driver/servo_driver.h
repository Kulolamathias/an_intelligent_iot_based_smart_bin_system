/**
 * @file components/drivers/actuators/servo_driver/servo_driver.h
 * @brief Servo Driver – hardware abstraction for PWM‑controlled servos (MG995).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to individual servo motors.
 * It uses the ESP‑IDF LEDC (PWM) peripheral to generate the control signal.
 *
 * It contains NO business logic, NO command handling, and NO event posting.
 * =============================================================================
 */

#ifndef SERVO_DRIVER_H
#define SERVO_DRIVER_H

#include "esp_err.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle representing a servo instance. */
typedef struct servo_handle_t *servo_handle_t;

/**
 * @brief Servo configuration structure.
 */
typedef struct {
    gpio_num_t gpio_num;        /**< GPIO pin for PWM signal */
    ledc_channel_t channel;     /**< LEDC channel (0..7) */
    ledc_timer_t timer;         /**< LEDC timer (LEDC_TIMER_0..3) */
    uint32_t min_pulse_us;      /**< Pulse width for 0° (typically 500 µs) */
    uint32_t max_pulse_us;      /**< Pulse width for 180° (typically 2500 µs) */
    uint32_t freq_hz;           /**< PWM frequency (50 Hz for standard servos) */
} servo_config_t;

/**
 * @brief Create a servo instance.
 *
 * @param cfg   Configuration (pins, channel, timer, pulse limits).
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t servo_driver_create(const servo_config_t *cfg, servo_handle_t *out_handle);

/**
 * @brief Set servo angle (blocking, waits for PWM to be applied).
 *
 * @param handle Servo instance handle.
 * @param angle_deg Target angle in degrees (0..180).
 * @return ESP_OK on success, error if handle invalid or angle out of range.
 */
esp_err_t servo_driver_set_angle(servo_handle_t handle, float angle_deg);

/**
 * @brief Stop the servo (set PWM duty cycle to 0, releasing the signal).
 *
 * @param handle Servo instance handle.
 * @return ESP_OK on success.
 */
esp_err_t servo_driver_stop(servo_handle_t handle);

/**
 * @brief Delete a servo instance and free resources.
 *
 * @param handle Servo instance handle.
 * @return ESP_OK on success.
 */
esp_err_t servo_driver_delete(servo_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_DRIVER_H */