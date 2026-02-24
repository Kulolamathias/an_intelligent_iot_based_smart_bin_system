#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi service.
 *
 * Creates internal queue, timer, and task. Initializes and starts the WiFi driver.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t wifi_service_init(void);

/**
 * @brief Register WiFi service command handlers with the command router.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t wifi_service_register_handlers(void);

/**
 * @brief Start WiFi service (no action, provided for lifecycle consistency).
 *
 * @return ESP_OK always.
 */
esp_err_t wifi_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SERVICE_H */