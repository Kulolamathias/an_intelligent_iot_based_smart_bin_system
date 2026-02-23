/**
 * @file buzzer_driver.h
 * @brief Low-level passive buzzer driver using LEDC PWM.
 *
 * Controls a buzzer with fixed 2kHz frequency, 50% duty when on.
 * No business logic, no timers, no service dependencies.
 */

#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the buzzer PWM.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t buzzer_driver_init(void);

/**
 * @brief Set buzzer state.
 * @param on true = buzzer on (2kHz, 50% duty), false = off.
 */
void buzzer_set(bool on);

/**
 * @brief (Optional) Change buzzer frequency.
 * @param freq_hz Desired frequency in Hz.
 * @return ESP_OK on success, error if not supported.
 */
esp_err_t buzzer_set_frequency(uint32_t freq_hz);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_DRIVER_H */