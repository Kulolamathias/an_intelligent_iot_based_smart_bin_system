/**
 * @file gps_driver.h
 * @brief GPS UART Driver – Low‑level NMEA Sentence Reader
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver encapsulates the ESP‑IDF UART driver for a GPS module.
 * It provides a simple interface to read complete NMEA sentences.
 * No parsing, no business logic.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: driver handle and public API.
 * - Does NOT: maintain any persistent state outside its own static context.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - All public functions validate arguments.
 * - The driver uses static instance pool; no dynamic allocation.
 * - read_line() blocks until a full line is received or timeout expires.
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-03-01
 * @author System Architecture Team
 * =============================================================================
 */

#ifndef GPS_DRIVER_H
#define GPS_DRIVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of GPS driver instances (static pool) */
#define GPS_DRIVER_MAX_INSTANCES 1

/** Opaque handle type for a GPS driver instance */
typedef int gps_driver_handle_t;

/** Invalid handle value */
#define GPS_DRIVER_HANDLE_INVALID (-1)

/**
 * @brief Create and initialize a GPS driver instance.
 *
 * Configures and installs the UART driver for the given pins.
 * Uses static instance pool; no dynamic allocation.
 *
 * @param uart_num  UART port number (e.g., UART_NUM_1)
 * @param baud_rate Baud rate (usually 9600)
 * @param tx_pin    GPIO pin for TX (or -1 if not used)
 * @param rx_pin    GPIO pin for RX
 * @param[out] handle Pointer to receive the driver handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_driver_create(int uart_num, int baud_rate, int tx_pin, int rx_pin,
                            gps_driver_handle_t *handle);

/**
 * @brief Start the GPS driver (enables UART reception).
 *
 * After this call, the UART is ready to receive data.
 *
 * @param handle Driver handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_driver_start(gps_driver_handle_t handle);

/**
 * @brief Stop the GPS driver (disables UART reception).
 *
 * @param handle Driver handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_driver_stop(gps_driver_handle_t handle);

/**
 * @brief Read a complete NMEA line (terminated by '\n').
 *
 * Blocks until a line is received or timeout expires.
 * The line is null‑terminated and does not include the '\n'.
 *
 * @param handle    Driver handle.
 * @param buffer    Output buffer to store the line.
 * @param max_len   Maximum buffer size (including null terminator).
 * @param timeout_ms Timeout in milliseconds.
 * @return Number of bytes read (excluding null) on success,
 *         -1 on timeout, -2 on error.
 */
int gps_driver_read_line(gps_driver_handle_t handle,
                         char *buffer, size_t max_len,
                         uint32_t timeout_ms);

/**
 * @brief Delete a GPS driver instance and free resources.
 *
 * Uninstalls the UART driver and releases the instance slot.
 *
 * @param handle Driver handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gps_driver_delete(gps_driver_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* GPS_DRIVER_H */