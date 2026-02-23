/**
 * @file example_service.h
 * @brief Example service demonstrating the base contract.
 *
 * =============================================================================
 * This service does nothing useful – it only illustrates the required
 * interface and safe interaction with core.
 * =============================================================================
 */

#ifndef EXAMPLE_SERVICE_H
#define EXAMPLE_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * PUBLIC API – Base Contract Implementation
 * ============================================================ */

/**
 * @brief Initialise example service.
 * @return ESP_OK on success.
 */
esp_err_t example_service_init(void);

/**
 * @brief Start example service.
 * @return ESP_OK on success.
 */
esp_err_t example_service_start(void);

/**
 * @brief Stop example service.
 * @return ESP_OK on success.
 */
esp_err_t example_service_stop(void);

/**
 * @brief Register command handlers with command_router.
 * @return ESP_OK on success.
 */
esp_err_t example_service_register_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_SERVICE_H */