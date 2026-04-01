/**
 * @file components/drivers/sensors/pir_driver/pir_driver.h
 * @brief PIR Motion Sensor Driver – hardware abstraction for HC‑SR501 (D SUN, 1000, RT, MD).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to a PIR motion sensor.
 * It configures the GPIO pin as input and reads the digital signal
 * (high = motion detected, low = no motion).
 *
 * It contains NO business logic, NO event posting, and NO decision making.
 * =============================================================================
 */

#ifndef PIR_DRIVER_H
#define PIR_DRIVER_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle representing a PIR sensor instance. */
typedef struct pir_handle_t *pir_handle_t;

/**
 * @brief Configuration structure for the PIR driver.
 */
typedef struct {
    gpio_num_t gpio_num;        /**< GPIO pin connected to PIR output */
} pir_config_t;

/**
 * @brief Create a PIR sensor instance.
 *
 * @param cfg   Configuration (GPIO pin).
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t pir_driver_create(const pir_config_t *cfg, pir_handle_t *out_handle);

/**
 * @brief Read the current motion state.
 *
 * @param handle PIR instance handle.
 * @param[out] motion_detected true if motion is currently detected (GPIO high).
 * @return ESP_OK on success, error if handle invalid.
 */
esp_err_t pir_driver_read(pir_handle_t handle, bool *motion_detected);

/**
 * @brief Delete a PIR sensor instance and free resources.
 *
 * @param handle PIR instance handle.
 * @return ESP_OK on success.
 */
esp_err_t pir_driver_delete(pir_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PIR_DRIVER_H */