/**
 * @file components/drivers/actuators/buzzer_driver/buzzer_driver.h
 * @brief Buzzer Driver – hardware abstraction for PWM‑controlled passive buzzers.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to one or more buzzers.
 * It uses the LEDC peripheral to generate PWM signals with configurable
 * frequency and duty cycle. It contains NO business logic, NO command handling,
 * and NO event posting.
 *
 * =============================================================================
 * DESIGN NOTES
 * =============================================================================
 * - Each buzzer instance uses its own LEDC channel and a dedicated timer
 *   (so frequency changes do not affect other buzzers).
 * - The driver supports starting a continuous tone and stopping it.
 * - Beep duration and pattern playback are handled at the service layer.
 * =============================================================================
 */

#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H

#include "esp_err.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle representing a buzzer instance. */
typedef struct buzzer_handle_t *buzzer_handle_t;

/**
 * @brief Configuration structure for the buzzer driver.
 */
typedef struct {
    gpio_num_t gpio_num;        /**< GPIO pin for the buzzer */
    ledc_channel_t channel;     /**< LEDC channel (0..7) */
    ledc_timer_t timer;         /**< LEDC timer (LEDC_TIMER_0..3) */
    uint32_t default_freq_hz;   /**< Default frequency (used if not set elsewhere) */
    uint8_t default_duty_percent; /**< Default duty cycle (0‑100) */
} buzzer_config_t;

/**
 * @brief Create a buzzer instance.
 *
 * @param cfg   Configuration (GPIO, channel, timer, defaults).
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t buzzer_driver_create(const buzzer_config_t *cfg, buzzer_handle_t *out_handle);

/**
 * @brief Start a continuous tone.
 *
 * @param handle Buzzer instance handle.
 * @param freq_hz Frequency in Hz.
 * @param duty_percent Duty cycle (0‑100). Typically 50 for square wave.
 * @return ESP_OK on success.
 */
esp_err_t buzzer_driver_start_tone(buzzer_handle_t handle, uint32_t freq_hz, uint8_t duty_percent);

/**
 * @brief Stop the buzzer (PWM off).
 *
 * @param handle Buzzer instance handle.
 * @return ESP_OK on success.
 */
esp_err_t buzzer_driver_stop(buzzer_handle_t handle);

/**
 * @brief Delete a buzzer instance and free resources.
 *
 * @param handle Buzzer instance handle.
 * @return ESP_OK on success.
 */
esp_err_t buzzer_driver_delete(buzzer_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_DRIVER_H */