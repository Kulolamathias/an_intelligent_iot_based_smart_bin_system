/**
 * @file components/services/sensing/pir_service/pir_service.h
 * @brief PIR Motion Service – detects presence and posts events.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service polls the PIR sensor at a fixed interval and posts
 * EVENT_PERSON_DETECTED or EVENT_PERSON_LEFT to the core when the motion
 * state changes. It contains no decision logic – only observation.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - CMD_START_PIR_MONITORING   – begin periodic polling (if not already)
 * - CMD_STOP_PIR_MONITORING    – stop polling
 *
 * =============================================================================
 * EVENTS POSTED
 * =============================================================================
 * - EVENT_PERSON_DETECTED      – motion detected (rising edge)
 * - EVENT_PERSON_LEFT          – no motion after having been detected (falling edge)
 * =============================================================================
 */

#ifndef PIR_SERVICE_H
#define PIR_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the PIR service (creates driver handle, timer).
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t pir_service_init(void);

/**
 * @brief Register command handlers with the command router.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t pir_service_register_handlers(void);

/**
 * @brief Start the PIR service (begins periodic polling).
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t pir_service_start(void);

/**
 * @brief Stop the PIR service (stops polling, deletes timer).
 * @return ESP_OK on success.
 */
esp_err_t pir_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* PIR_SERVICE_H */