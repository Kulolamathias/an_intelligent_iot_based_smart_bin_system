/**
 * @file   components/drivers/sensors/gps/gps_driver.h
 * @brief  GPS Driver – UART hardware abstraction for NEO‑xM modules.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver owns the UART peripheral connected to the GPS module. It
 * provides a minimal, deterministic interface to initialise the UART, read
 * raw bytes, and release the hardware resource.
 *
 * The driver contains NO business logic, NO NMEA parsing, NO event posting,
 * and NO knowledge of any other component. It is a hardware abstraction
 * layer only.
 *
 * Responsibilities:
 *  - Configure UART port (baud, pins, buffer size)
 *  - Provide non‑blocking byte‑read access
 *  - Clean‑up UART on deinit
 *
 * Forbidden:
 *  - NMEA sentence processing
 *  - Event posting or command handling
 *  - Dynamic memory allocation after init
 *  - Blocking operations that exceed the caller‑specified timeout
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * Owns:
 *  - UART port, pin assignments, static UART configuration
 *
 * Does NOT own:
 *  - Any other peripheral
 *  - Parsed GPS data or system state
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - Exactly one UART instance is managed (UART_NUM_2, 9600 baud, 8N1)
 * - Read operations never block indefinitely; a timeout is always enforced
 * - All buffers are supplied by the caller (zero‑copy)
 * - The driver is stateless — init/deinit are idempotent
 *
 * @author  Matthithyahu
 * @date    2026/05/11
 * @version 1.0.0
 */

#ifndef GPS_DRIVER_H
#define GPS_DRIVER_H

#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Public types                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configuration structure for the GPS driver (hard‑coded defaults).
 *
 * All fields are compile‑time constants; the struct is provided for
 * clarity and future Kconfig integration.
 */
typedef struct {
    uart_port_t uart_num;        /**< UART peripheral instance (UART_NUM_2) */
    gpio_num_t  tx_pin;          /**< GPIO connected to GPS RX */
    gpio_num_t  rx_pin;          /**< GPIO connected to GPS TX */
    int         baud_rate;       /**< Communication speed (9600) */
    size_t      rx_buffer_size;  /**< UART hardware RX ring buffer size */
} gps_driver_config_t;

/* -------------------------------------------------------------------------- */
/* Public API                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialise the GPS UART hardware.
 *
 * Configures and installs the UART driver according to the provided config.
 * Must be called once before any read operation.
 *
 * @param cfg  Pointer to configuration (pins, baud, buffer size).
 * @return
 *   - ESP_OK              Initialisation successful
 *   - ESP_ERR_INVALID_ARG cfg is NULL
 *   - Other error codes from uart_driver_install / uart_param_config
 */
esp_err_t gps_driver_init(const gps_driver_config_t *cfg);

/**
 * @brief Read raw bytes from the GPS module.
 *
 * Reads up to @p max_len bytes from the internal UART RX buffer. If fewer
 * bytes are available, the function returns after @p timeout_ms or when
 * sufficient data arrives. This is a non‑blocking call: it never waits for
 * more data than already present and times out after the specified period.
 *
 * @param[out] buf        Destination buffer (caller‑allocated).
 * @param[in]  max_len    Maximum number of bytes to read.
 * @param[out] out_len    Actual number of bytes placed in @p buf.
 * @param[in]  timeout_ms Maximum time to wait for data, in milliseconds
 *                        (0 = return immediately with available bytes).
 * @return
 *   - ESP_OK              At least 1 byte was read
 *   - ESP_ERR_TIMEOUT     No data available within @p timeout_ms
 *   - ESP_ERR_INVALID_ARG buf or out_len is NULL
 *   - ESP_ERR_INVALID_STATE if driver not initialised
 */
esp_err_t gps_driver_read(uint8_t *buf, size_t max_len, size_t *out_len,
                          uint32_t timeout_ms);

/**
 * @brief Deinitialise the GPS UART and release hardware resources.
 *
 * Stops and uninstalls the UART driver. After this call the module must
 * be re‑initialised before further reads.
 *
 * @return
 *   - ESP_OK              Deinitialisation complete
 *   - ESP_ERR_INVALID_STATE if driver was not initialised
 */
esp_err_t gps_driver_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* GPS_DRIVER_H */