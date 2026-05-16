/**
 * @file components/services/connectivity/gsm/gsm_service.h
 * @brief GSM Service – SMS sending, authentication, event posting.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the GSM driver and provides a command interface for:
 * - Sending SMS (CMD_SEND_SMS, CMD_SEND_SMS_RESPONSE)
 * - Setting password and authorised numbers (CMD_SET_GSM_PASSWORD,
 *   CMD_SET_AUTHORIZED_NUMBERS)
 * - Starting/stopping GSM (CMD_CONNECT_GSM, CMD_DISCONNECT_GSM)
 *
 * It runs a background task that:
 * - Polls the driver for incoming SMS
 * - Checks password and authorised number
 * - Posts EVENT_AUTH_GRANTED or EVENT_AUTH_DENIED to the core
 *
 * It contains NO core state decisions – only observation (post events) and
 * command execution (send SMS).
 *
 * =============================================================================
 * DEPENDENCIES
 * =============================================================================
 * - gsm_driver (low‑level UART/AT commands)
 * - service_interfaces (event posting, command registration)
 *
 * @version 1.0.0
 * @author System Architecture Team
 * @date 2026
 */

#ifndef GSM_SERVICE_H
#define GSM_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the GSM service.
 *
 * Creates internal queue, task, and configuration storage.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gsm_service_init(void);

/**
 * @brief Register GSM command handlers with the command router.
 *
 * Handles:
 * - CMD_SEND_SMS
 * - CMD_SEND_SMS_RESPONSE
 * - CMD_SET_GSM_PASSWORD
 * - CMD_SET_AUTHORIZED_NUMBERS
 * - CMD_CONNECT_GSM
 * - CMD_DISCONNECT_GSM
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gsm_service_register_handlers(void);

/**
 * @brief Start the GSM service.
 *
 * Begins the background task that polls for incoming SMS.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t gsm_service_start(void);

/**
 * @brief Stop the GSM service (gracefully).
 *
 * Stops the background task and optionally disconnects GSM.
 *
 * @return ESP_OK on success.
 */
esp_err_t gsm_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM_SERVICE_H */