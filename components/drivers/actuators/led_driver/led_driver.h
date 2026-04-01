/**
 * @file components/drivers/actuators/led_driver/led_driver.h
 * @brief LED Driver – hardware abstraction for PWM‑controlled LEDs.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to control one or more LEDs
 * using LEDC (PWM). It supports brightness (0‑100%), on/off, toggle, blink,
 * and fade transitions. It contains NO business logic, NO command handling,
 * and NO event posting.
 *
 * =============================================================================
 * DESIGN NOTES
 * =============================================================================
 * - Each LED instance uses a dedicated LEDC channel.
 * - Timers can be shared among channels if the same frequency is used.
 * - Blinking and fading are implemented using esp_timer callbacks.
 * =============================================================================
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "esp_err.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle representing an LED instance. */
typedef struct led_handle_t *led_handle_t;

/**
 * @brief Configuration structure for the LED driver.
 */
typedef struct {
    gpio_num_t gpio_num;        /**< GPIO pin for the LED */
    ledc_channel_t channel;     /**< LEDC channel (0..7) */
    ledc_timer_t timer;         /**< LEDC timer (LEDC_TIMER_0..3) */
    uint32_t freq_hz;           /**< PWM frequency (typically 1 kHz for smooth dimming) */
    bool active_high;           /**< true = high level turns LED on (normal), false = inverted */
} led_config_t;

/**
 * @brief Create an LED instance.
 *
 * @param cfg   Configuration (GPIO, channel, timer, frequency, active level).
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t led_driver_create(const led_config_t *cfg, led_handle_t *out_handle);

/**
 * @brief Set LED brightness (0‑100%).
 *
 * @param handle LED instance handle.
 * @param percent Brightness percentage (0 = off, 100 = full).
 * @return ESP_OK on success.
 */
esp_err_t led_driver_set_brightness(led_handle_t handle, uint8_t percent);

/**
 * @brief Turn LED on (100% brightness).
 *
 * @param handle LED instance handle.
 * @return ESP_OK on success.
 */
esp_err_t led_driver_on(led_handle_t handle);

/**
 * @brief Turn LED off (0% brightness).
 *
 * @param handle LED instance handle.
 * @return ESP_OK on success.
 */
esp_err_t led_driver_off(led_handle_t handle);

/**
 * @brief Toggle LED (if on → off, if off → on).
 *
 * @param handle LED instance handle.
 * @return ESP_OK on success.
 */
esp_err_t led_driver_toggle(led_handle_t handle);

/**
 * @brief Start blinking with a given period and duty cycle.
 *
 * This uses a software timer to toggle the LED. Blink continues until
 * led_driver_stop_blink() is called.
 *
 * @param handle LED instance handle.
 * @param period_ms Blink period in milliseconds (time for one full cycle on+off).
 * @param duty_percent Percentage of time on (0‑100). 50 = equal on/off.
 * @return ESP_OK on success.
 */
esp_err_t led_driver_start_blink(led_handle_t handle, uint32_t period_ms, uint8_t duty_percent);

/**
 * @brief Stop blinking and restore last steady brightness.
 *
 * @param handle LED instance handle.
 * @return ESP_OK on success.
 */
esp_err_t led_driver_stop_blink(led_handle_t handle);

/**
 * @brief Start a smooth fade from current brightness to target brightness.
 *
 * @param handle LED instance handle.
 * @param target_percent Target brightness (0‑100).
 * @param duration_ms Duration of the fade in milliseconds.
 * @return ESP_OK on success.
 */
esp_err_t led_driver_start_fade(led_handle_t handle, uint8_t target_percent, uint32_t duration_ms);

/**
 * @brief Stop any ongoing fade.
 *
 * @param handle LED instance handle.
 * @return ESP_OK on success.
 */
esp_err_t led_driver_stop_fade(led_handle_t handle);

/**
 * @brief Delete an LED instance and free resources.
 *
 * @param handle LED instance handle.
 * @return ESP_OK on success.
 */
esp_err_t led_driver_delete(led_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* LED_DRIVER_H */