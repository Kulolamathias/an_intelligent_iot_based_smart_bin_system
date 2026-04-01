/**
 * @file components/drivers/sensors/gps_driver/gps_driver.h
 * @brief GPS Driver – hardware abstraction for NMEA‑0183 compatible GPS modules (NEO‑6M, NEO‑7M, NEO‑8M).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to a GPS module connected via UART.
 * It reads raw NMEA sentences, parses selected sentences ($GPGGA, $GPRMC),
 * and provides structured data (position, time, fix quality, satellites, etc.).
 *
 * It contains NO business logic, NO command handling, and NO event posting.
 * =============================================================================
 */

#ifndef GPS_DRIVER_H
#define GPS_DRIVER_H

#include "esp_err.h"
#include "driver/uart.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle representing a GPS instance. */
typedef struct gps_handle_t *gps_handle_t;

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

/**
 * @brief GPS module configuration.
 */
typedef struct {
    uart_port_t uart_num;       /**< UART port (UART_NUM_0, UART_NUM_1, UART_NUM_2) */
    gpio_num_t tx_pin;          /**< UART TX pin (connect to GPS RX) */
    gpio_num_t rx_pin;          /**< UART RX pin (connect to GPS TX) */
    uint32_t baud_rate;         /**< Baud rate (default for NEO‑6M is 9600) */
    uint32_t rx_buffer_size;    /**< Size of UART RX buffer (bytes) */
} gps_config_t;

/**
 * @brief Create a GPS instance.
 *
 * @param cfg   Configuration (UART, pins, baud rate).
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_driver_create(const gps_config_t *cfg, gps_handle_t *out_handle);

/**
 * @brief Start GPS module (enable NMEA output, clear buffers).
 *
 * @param handle GPS instance handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_driver_start(gps_handle_t handle);

/**
 * @brief Read and parse available data from GPS (non‑blocking).
 *
 * This function reads from the UART buffer and parses NMEA sentences
 * as they become available. It updates the internal data structure.
 *
 * @param handle GPS instance handle.
 * @return ESP_OK if at least one sentence was parsed, ESP_ERR_TIMEOUT if nothing read.
 */
esp_err_t gps_driver_update(gps_handle_t handle);

/**
 * @brief Get the latest parsed GPS data.
 *
 * @param handle GPS instance handle.
 * @param[out] data Pointer to store the data.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_driver_get_data(gps_handle_t handle, gps_data_t *data);

/**
 * @brief Stop GPS module (disable NMEA output, but keep UART).
 *
 * @param handle GPS instance handle.
 * @return ESP_OK on success.
 */
esp_err_t gps_driver_stop(gps_handle_t handle);

/**
 * @brief Delete a GPS instance and free resources.
 *
 * @param handle GPS instance handle.
 * @return ESP_OK on success.
 */
esp_err_t gps_driver_delete(gps_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* GPS_DRIVER_H */