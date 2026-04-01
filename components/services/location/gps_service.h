/**
 * @file gps_service.h
 * @brief GPS Service – NMEA Parsing and Fix Management
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the GPS receiver. It reads NMEA sentences from the driver,
 * parses relevant messages ($GPGGA, $GPRMC), maintains a current fix,
 * and emits events when the fix changes or is lost.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: public API for the service manager (init, register_handlers, start).
 * - Does NOT: directly access UART; uses the GPS driver.
 *
 * =============================================================================
 * DEPENDENCIES
 * =============================================================================
 * - gps_driver (low‑level UART)
 * - service_interfaces (command registration, event posting)
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-03-01
 * @author System Architecture Team
 * =============================================================================
 */

#ifndef GPS_SERVICE_H
#define GPS_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the GPS service.
 *
 * Creates the internal queue, task, and initializes the GPS driver.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_service_init(void);

/**
 * @brief Register GPS service command handlers with the command router.
 *
 * Handles CMD_GPS_START, CMD_GPS_STOP, CMD_GPS_GET_LAST_FIX.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_service_register_handlers(void);

/**
 * @brief Start the GPS service.
 *
 * This function does nothing; the service waits for commands.
 *
 * @return ESP_OK always.
 */
esp_err_t gps_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* GPS_SERVICE_H */