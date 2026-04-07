/**
 * @file components/common_utils/include/gps_types.h
 * @brief Shared GPS data structures.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This header defines the common data structure used by the GPS driver,
 * GPS service, and any other component that needs GPS data. It contains no
 * hardware abstraction or core logic.
 * =============================================================================
 */

#ifndef GPS_TYPES_H
#define GPS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPS data structure (parsed from NMEA sentences).
 */
typedef struct {
    bool fix_valid;             /**< true if fix is usable (3D fix) */
    bool time_valid;            /**< true if time/date is valid */
    double latitude;            /**< decimal degrees, positive north */
    double longitude;           /**< decimal degrees, positive east */
    float altitude_m;           /**< altitude above mean sea level (meters) */
    float speed_kmh;            /**< ground speed in km/h */
    float course_deg;           /**< course over ground (degrees) */
    uint8_t satellites;         /**< number of satellites used */
    float hdop;                 /**< horizontal dilution of precision */
    uint32_t timestamp_ms;      /**< system time when fix was obtained (ms) */
    /* Time from NMEA (UTC) */
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t year;
    uint8_t month;
    uint8_t day;
} gps_data_t;

#ifdef __cplusplus
}
#endif

#endif /* GPS_TYPES_H */