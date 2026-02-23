/**
 * @file led_driver.h
 * @brief Low-level LED driver for ESP32.
 *
 * Controls a single LED on a GPIO pin.
 * No business logic, no timers, no service dependencies.
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the LED GPIO.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t led_driver_init(void);

/**
 * @brief Set LED state.
 * @param on true = LED on, false = LED off.
 */
void led_set(bool on);

#ifdef __cplusplus
}
#endif

#endif /* LED_DRIVER_H */