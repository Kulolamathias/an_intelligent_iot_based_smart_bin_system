/**
 * @file indicator_service.h
 * @brief Indicator Service – manages LED, buzzer, and LCD backlight.
 *
 * =============================================================================
 * This service controls visual/acoustic indicators based on pattern commands.
 * Patterns are encoded as:
 *   0 = off
 *   1 = solid on
 *   2 = slow blink (500ms on/off)
 *   3 = fast blink (200ms on/off)
 *
 * It uses FreeRTOS timers to implement blinking without blocking.
 * =============================================================================
 */

#ifndef INDICATOR_SERVICE_H
#define INDICATOR_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Public API – Base Contract
 * ============================================================ */

/**
 * @brief Initialise indicator service (creates timers).
 * @return ESP_OK on success.
 */
esp_err_t indicator_service_init(void);

/**
 * @brief Start indicator service (no action).
 * @return ESP_OK.
 */
esp_err_t indicator_service_start(void);

/**
 * @brief Stop indicator service (stops all indicators, turns them off).
 * @return ESP_OK.
 */
esp_err_t indicator_service_stop(void);

/**
 * @brief Register command handlers with command_router.
 * @return ESP_OK on success.
 */
esp_err_t indicator_service_register_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* INDICATOR_SERVICE_H */