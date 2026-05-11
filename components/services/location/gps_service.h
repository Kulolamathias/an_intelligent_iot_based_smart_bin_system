/**
 * @file   components/services/location/gps_service.h
 * @brief  GPS Service – NMEA parsing, fix acquisition, event posting.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the GPS data pipeline from raw UART bytes to structured
 * fix information. It:
 *  - Periodically polls the GPS driver (non‑blocking)
 *  - Accumulates and parses NMEA sentences using a deterministic comma‑split
 *    parser (proven with NEO‑6M/7M/8M)
 *  - Stores the latest valid fix (thread‑safe)
 *  - Posts EVENT_GPS_FIX_UPDATE and EVENT_GPS_FIX_LOST to the core
 *
 * It receives commands (CMD_GPS_START, CMD_GPS_STOP) from the core and
 * executes them. It contains NO system‑level decisions, NO direct calls to
 * other services, and NO knowledge of the core state machine.
 *
 * Responsibilities:
 *  - Manage esp_timer for periodic reading
 *  - NMEA line buffering and parsing (GGA / RMC, any talker ID)
 *  - Provide thread‑safe access to the latest GPS fix (internal use only)
 *  - Post events as observable facts
 *
 * Forbidden:
 *  - Making system‑level decisions (e.g., when to start/stop GPS – that is a
 *    core decision)
 *  - Calling other services directly
 *  - Accessing hardware without the driver
 *  - Dynamic memory allocation after init
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * Owns:
 *  - gps_driver instance
 *  - esp_timer handle
 *  - NMEA line buffer and parsing state
 *  - Mutex‑protected gps_fix_t storage
 *
 * Does NOT own:
 *  - Core context or system state
 *  - Other services or peripherals
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - The parser is deterministic: given the same NMEA input, it always
 *   produces the same output.
 * - The service never blocks internally; all driver reads are non‑blocking
 *   with a 5 ms timeout.
 * - The timer period is 200 ms (5 Hz), sufficient for NMEA output intervals.
 * - The mutex is held only for copying `last_fix`; no parsing or logging
 *   occurs under the lock.
 * - Fix‑loss detection requires N consecutive timeouts (default 10 s without
 *   a valid fix) before posting EVENT_GPS_FIX_LOST.
 *
 * =============================================================================
 * FUTURE EXTENSION
 * =============================================================================
 * The service will later support coordinate‑to‑place‑name mapping via a
 * local lookup table (NVS) and/or remote geocoding API. Injection points for
 * name enrichment are marked with comments containing "PLACE_NAME_FEATURE".
 *
 * @author  Matthithyahu
 * @date    2026/05/11
 * @version 1.0.0
 */

#ifndef GPS_SERVICE_H
#define GPS_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- Public API (called by service_manager) -------- */

esp_err_t gps_service_init(void);
esp_err_t gps_service_register_handlers(void);
esp_err_t gps_service_start(void);


#ifdef __cplusplus
}
#endif

#endif /* GPS_SERVICE_H */