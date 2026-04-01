/**
 * @file components/services/actuation/led_service/led_service.h
 * @brief LED Service – manages one or more LEDs via commands.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service provides a command interface to control LEDs using the LED driver.
 * It receives commands from the command router and executes them on the
 * appropriate LED instance. It posts no events (pure actuation).
 * =============================================================================
 */

#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the LED service (creates LED instances).
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t led_service_init(void);

/**
 * @brief Register command handlers with the command router.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t led_service_register_handlers(void);

/**
 * @brief Start the LED service (no background tasks).
 * @return ESP_OK always.
 */
esp_err_t led_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_SERVICE_H */