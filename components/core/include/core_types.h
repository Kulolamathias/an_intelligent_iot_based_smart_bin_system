/**
 * @file core_types.h
 * @brief Core Type Definitions – Shared Data Structures
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module provides common type definitions used across multiple core
 * components and, where necessary, by services and drivers.
 *
 * These types represent:
 * - Physical quantities (GPS coordinates)
 * - Status enumerations (authentication, error flags)
 * - External data structures (neighbor bin information)
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: shared data types with no dependencies.
 * - Does NOT: contain any logic, state, or executable code.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - No core logic depends on the internal layout of these structures.
 * - All types are serialization-friendly (no pointers, fixed size).
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------
 * GPS Coordinate Representation
 * ------------------------------------------------------------ */
typedef struct {
    double latitude;          /**< Decimal degrees, positive north */
    double longitude;         /**< Decimal degrees, positive east */
    double altitude;          /**< Meters above sea level */
    uint8_t fix_quality;      /**< 0 = invalid, 1 = GPS fix, 2 = DGPS */
} gps_coordinates_t;

/* ------------------------------------------------------------
 * Authentication Status
 * ------------------------------------------------------------ */
typedef enum {
    AUTH_STATUS_NONE = 0,     /**< No authentication attempted */
    AUTH_STATUS_PENDING,      /**< Authentication in progress */
    AUTH_STATUS_GRANTED,      /**< Credentials accepted */
    AUTH_STATUS_DENIED        /**< Credentials rejected */
} auth_status_t;

/* ------------------------------------------------------------
 * Error Flags (bitmask)
 * ------------------------------------------------------------ */
typedef uint32_t error_flags_t;
#define ERROR_FLAG_NONE         (0U)      /**< No active errors */
#define ERROR_FLAG_SENSOR       (1U << 0) /**< Sensor failure */
#define ERROR_FLAG_COMMS        (1U << 1) /**< Network connectivity lost */
#define ERROR_FLAG_ACTUATOR     (1U << 2) /**< Actuator (servo) fault */
#define ERROR_FLAG_CONFIG       (1U << 3) /**< Configuration corrupted */

/* ------------------------------------------------------------
 * Neighbor Bin Information (received via bin-to-bin communication)
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t mac[6];                    /**< MAC address of peer bin */
    gps_coordinates_t location;        /**< Peer's location */
    uint8_t fill_level_percent;        /**< Peer's fill level */
    bool online;                      /**< Peer reachable */
} neighbor_bin_info_t;

/* ------------------------------------------------------------
 * Servo Identification (for multi‑servo systems)
 * ------------------------------------------------------------ */
typedef enum {
    SERVO_LID = 0,      /**< Main bin lid servo */
    SERVO_AUX,          /**< Reserved for future auxiliary servos */
    SERVO_MAX
} servo_id_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_TYPES_H */