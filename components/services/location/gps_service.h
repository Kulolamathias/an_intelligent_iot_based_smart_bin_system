/**
 * @file components/services/location/gps_service/gps_service.h
 * @brief GPS Service – acquires location data and enriches it with human‑readable names.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the GPS driver and provides a command interface to start/stop
 * GPS acquisition, retrieve location data, and maintain a mapping of coordinates
 * to human‑readable place names. It emits enriched location events to the core.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - Handles raw GPS data (lat, lon, altitude, etc.) from the driver.
 * - Enriches data with a place name using:
 *   1. Local mapping table (pre‑registered coordinate areas)
 *   2. (Future) External reverse geocoding API via MQTT/HTTP
 * - Allows dynamic addition of known locations via commands.
 * - Designed for extensibility: mapping table can be stored in NVS, names can be
 *   updated remotely, and external geocoding can be integrated later.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - CMD_GPS_START               – power on GPS and begin reading
 * - CMD_GPS_STOP                – power off GPS
 * - CMD_GPS_GET_LAST_FIX        – retrieve most recent location data (immediate)
 * - CMD_GPS_SET_LOCATION_NAME   – assign a name to the current location
 * - CMD_GPS_ADD_KNOWN_LOCATION  – add/update a mapping of coordinates to a name
 *
 * =============================================================================
 * EVENTS POSTED
 * =============================================================================
 * - EVENT_GPS_FIX_UPDATED       – new location fix available (with coordinates + name)
 * - EVENT_GPS_FIX_LOST          – GPS fix lost
 * - EVENT_GPS_LOCATION_NAMED    – when a name is assigned to the current location
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-01
 */

#ifndef GPS_SERVICE_H
#define GPS_SERVICE_H

#include <stdbool.h>
#include "esp_err.h"
#include "gps_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the GPS service.
 *
 * Creates the internal task, queue, and initialises the GPS driver.
 * Also loads the known location table from NVS (if available).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_service_init(void);

/**
 * @brief Register GPS service command handlers with the command router.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_service_register_handlers(void);

/**
 * @brief Start the GPS service.
 *
 * Powers on the GPS module and begins periodic reading (in the service task).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_service_start(void);

/**
 * @brief Stop the GPS service.
 *
 * Powers off the GPS module and stops the reading task.
 *
 * @return ESP_OK on success.
 */
esp_err_t gps_service_stop(void);

/**
 * @brief Get the latest GPS fix (for internal use by other services).
 * @param[out] data Pointer to store the data.
 * @return true if fix is valid, false otherwise.
 */
bool gps_service_get_last_fix(gps_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* GPS_SERVICE_H */