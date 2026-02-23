/**
 * @file gsm_sim800_driver.h
 * @brief Low-level driver for SIM800 GSM module (UART AT commands).
 *
 * =============================================================================
 * PURE HARDWARE ABSTRACTION – no business logic, no authentication, no queuing.
 * =============================================================================
 */

#ifndef GSM_SIM800_DRIVER_H
#define GSM_SIM800_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Configuration
 * ============================================================ */
typedef struct {
    uart_port_t uart_port;      /**< UART port number */
    int tx_pin;                 /**< TX GPIO pin */
    int rx_pin;                 /**< RX GPIO pin */
    int baud_rate;              /**< Baud rate (default 9600) */
    int buf_size;               /**< UART buffer size */
    uint32_t cmd_timeout_ms;    /**< Default command timeout */
    uint8_t retry_count;        /**< AT command retry count */
} gsm_driver_config_t;

/* ============================================================
 * Raw SMS structure (driver-level parsing only)
 * ============================================================ */
typedef struct {
    uint8_t index;              /**< SMS storage index in SIM */
    char sender[16];            /**< Sender phone number (cleaned) */
    char message[161];          /**< Message content (null-terminated) */
    char timestamp[32];         /**< Timestamp string (raw) */
} gsm_raw_sms_t;

/* ============================================================
 * Driver API – no business logic, just hardware commands
 * ============================================================ */

/**
 * @brief Initialise GSM driver, configure UART, and perform basic module setup.
 * @param config Driver configuration (NULL for defaults).
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t gsm_driver_init(const gsm_driver_config_t *config);

/**
 * @brief Deinitialise GSM driver (free resources, stop tasks).
 */
esp_err_t gsm_driver_deinit(void);

// /**
//  * @brief Send a raw AT command and wait for expected response.
//  * @param cmd         Command string (without \r).
//  * @param expected    Expected response substring (e.g., "OK").
//  * @param timeout_ms  Timeout in milliseconds.
//  * @param response    Optional buffer to store full response (can be NULL).
//  * @param resp_size   Size of response buffer.
//  * @return ESP_OK if expected response received, ESP_FAIL or timeout otherwise.
//  */
// esp_err_t gsm_driver_send_at(const char *cmd, const char *expected,
//                              uint32_t timeout_ms, char *response, size_t resp_size);

/**
 * @brief Send an SMS (blocking).
 * @param number   Recipient phone number.
 * @param message  SMS text.
 * @param timeout_ms Overall timeout for the operation.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t gsm_driver_send_sms(const char *number, const char *message, uint32_t timeout_ms);

/**
 * @brief Read an SMS by index.
 * @param index  Storage index (1-based).
 * @param out    Pointer to raw SMS structure to fill.
 * @return ESP_OK if read successfully, ESP_FAIL if not found or parse error.
 */
esp_err_t gsm_driver_read_sms(uint8_t index, gsm_raw_sms_t *out);

/**
 * @brief Delete an SMS by index.
 * @param index  Storage index.
 * @return ESP_OK on success.
 */
esp_err_t gsm_driver_delete_sms(uint8_t index);

/**
 * @brief List all SMS (raw response). Use to discover indices.
 * @param response_buffer  Buffer to store raw +CMGL response.
 * @param buffer_size      Size of buffer.
 * @return ESP_OK on success.
 */
esp_err_t gsm_driver_list_sms(char *response_buffer, size_t buffer_size);

/**
 * @brief Reset GSM module (AT command).
 * @return ESP_OK if module responds after reset.
 */
esp_err_t gsm_driver_reset(void);

/**
 * @brief Check if module is ready (send basic AT).
 */
bool gsm_driver_is_ready(void);

/**
 * @brief Parse +CMGL response to extract SMS indices.
 * @param resp      Raw +CMGL response string.
 * @param indices   Output array to store indices.
 * @param count     Output variable to store number of indices found.
 * @param max       Maximum number of indices to store.
 * 
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if max is 0 or resp is NULL.
 */
esp_err_t parse_cmgl_response(const char *resp, uint8_t *indices, int *count, int max);

/**
 * @brief Check if GSM module is registered on the network.
 * @return true if registered (home or roaming), false otherwise.
 */
bool gsm_driver_is_registered(void);

/**
 * @brief Send a raw AT command and wait for expected response.
 * @param cmd         Command string (without \r).
 * @param expected    Expected response substring (e.g., "OK").
 * @param timeout_ms  Timeout in milliseconds.
 * @param response    Optional buffer to store full response (can be NULL).
 * @param resp_size   Size of response buffer.
 * @param quiet       If true, suppress error logging (for periodic checks).
 * @return ESP_OK if expected response received, ESP_FAIL or timeout otherwise.
 */
esp_err_t gsm_driver_send_at_quiet(const char *cmd, const char *expected,
                                   uint32_t timeout_ms, char *response, size_t resp_size,
                                   bool quiet);

// Keep old name for backward compatibility
#define gsm_driver_send_at(cmd, expected, timeout, resp, size) \
    gsm_driver_send_at_quiet(cmd, expected, timeout, resp, size, false)

/**
 * @brief Simple ping to check if module responds.
 * @return true if module responds to AT.
 */
bool gsm_driver_ping(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM_SIM800_DRIVER_H */