#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MQTT service.
 *
 * Creates internal queue, timer, and task. Initializes the MQTT client abstraction
 * with default configuration (later overridden by CMD_SET_CONFIG_MQTT).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mqtt_service_init(void);

/**
 * @brief Register MQTT service command handlers with the command router.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mqtt_service_register_handlers(void);

/**
 * @brief Start MQTT service (no action, provided for lifecycle consistency).
 *
 * @return ESP_OK always.
 */
esp_err_t mqtt_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_SERVICE_H */