/**
 * @file components/drivers/comms/gsm_sim800/gsm_driver.h
 * @brief GSM SIM800 Driver – hardware abstraction for SIM800/SIM900 modules.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to a GSM module connected via UART.
 * It handles low‑level AT command sending, response reading, and SMS sending.
 *
 * It contains NO business logic, NO authentication, NO event posting, and NO
 * command handling. It is a pure hardware abstraction layer.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Owns: UART driver instance, handle structure.
 * - Provides: init/deinit, AT command exchange, SMS sending.
 * - Does NOT: parse SMS content, make decisions, post events.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - Each handle corresponds to one UART port.
 * - All public functions validate arguments.
 * - Responses are null‑terminated strings.
 *
 * @version 1.0.0
 * @author System Architecture Team
 * @date 2026
 */

#ifndef GSM_DRIVER_H
#define GSM_DRIVER_H

#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle representing a GSM module instance. */
typedef struct gsm_driver_handle_t *gsm_handle_t;

/**
 * @brief GSM driver configuration structure.
 */
typedef struct {
    uart_port_t uart_num;          /**< UART port (UART_NUM_1, UART_NUM_2) */
    gpio_num_t tx_pin;             /**< UART TX pin (connect to GSM RX) */
    gpio_num_t rx_pin;             /**< UART RX pin (connect to GSM TX) */
    uint32_t baud_rate;            /**< Baud rate (default 9600 for SIM800) */
    size_t rx_buffer_size;         /**< UART RX buffer size (bytes) */
    uint32_t cmd_timeout_ms;       /**< Default timeout for AT commands (ms) */
    uint8_t retry_count;           /**< Number of retries for critical commands */
} gsm_driver_config_t;

/**
 * @brief Create a GSM driver instance.
 *
 * Initialises the UART and performs basic communication test (AT).
 *
 * @param config Driver configuration (pins, UART, timeouts).
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gsm_driver_create(const gsm_driver_config_t *config, gsm_handle_t *out_handle);

/**
 * @brief Send an AT command and wait for a response.
 *
 * This function is blocking. It sends `cmd` (automatically appends "\r"),
 * then waits for a string containing `expected_response` or a timeout.
 * The full response (up to resp_size-1 bytes) is stored in `response`.
 *
 * @param handle GSM instance handle.
 * @param cmd AT command (without "\r", e.g., "AT").
 * @param expected_response Substring to wait for (e.g., "OK").
 * @param response Buffer to store the full response (may be NULL).
 * @param resp_size Size of response buffer (ignored if response == NULL).
 * @param timeout_ms Timeout in milliseconds.
 * @return ESP_OK if expected response found, ESP_ERR_TIMEOUT if not,
 *         other error codes on UART failure.
 */
esp_err_t gsm_driver_send_at(gsm_handle_t handle,
                             const char *cmd,
                             const char *expected_response,
                             char *response,
                             size_t resp_size,
                             uint32_t timeout_ms);

/**
 * @brief Read a single line from the GSM UART (non‑blocking).
 *
 * Reads until a newline character (\n) is encountered. If no line is
 * available within `timeout_ms`, returns ESP_ERR_TIMEOUT.
 *
 * @param handle GSM instance handle.
 * @param line Buffer to store the line (null‑terminated).
 * @param line_size Size of line buffer.
 * @param timeout_ms Maximum time to wait for a line (milliseconds).
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no line, other error codes.
 */
esp_err_t gsm_driver_read_line(gsm_handle_t handle,
                               char *line,
                               size_t line_size,
                               uint32_t timeout_ms);

/**
 * @brief Send an SMS (low‑level, blocking).
 *
 * Sends an SMS using the AT+CMGS command. This function blocks until the
 * SMS is sent or a timeout occurs. It does not perform retries or queuing.
 *
 * @param handle GSM instance handle.
 * @param phone_number Recipient phone number (string).
 * @param message Message text (max 160 characters).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gsm_driver_send_sms(gsm_handle_t handle,
                              const char *phone_number,
                              const char *message);

/**
 * @brief Delete a GSM driver instance and free resources.
 *
 * Stops the UART driver and releases the handle.
 *
 * @param handle GSM instance handle.
 * @return ESP_OK on success.
 */
esp_err_t gsm_driver_delete(gsm_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* GSM_DRIVER_H */