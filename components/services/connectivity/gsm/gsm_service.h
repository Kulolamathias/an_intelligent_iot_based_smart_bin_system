/**
 * @file gsm_service.h
 * @brief GSM Service – manages authentication, SMS queue, and event generation.
 *
 * =============================================================================
 * Follows base service contract. Uses GSM driver for hardware interaction.
 * No business logic – only translates SMS to core events and handles commands.
 * =============================================================================
 */

#ifndef GSM_SERVICE_H
#define GSM_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Base contract functions
 * ============================================================ */
esp_err_t gsm_service_init(void);
esp_err_t gsm_service_start(void);
esp_err_t gsm_service_stop(void);
esp_err_t gsm_service_register_handlers(void);

/* ============================================================
 * Configuration (to be called after init, before start)
 * ============================================================ */

/**
 * @brief Set the password that must appear in SMS commands.
 */
void gsm_service_set_password(const char *password);

/**
 * @brief Set list of authorized phone numbers.
 * @param numbers  Array of null-terminated strings.
 * @param count    Number of entries.
 */
void gsm_service_set_authorized_numbers(const char **numbers, uint8_t count);

#ifdef __cplusplus
}
#endif

#endif /* GSM_SERVICE_H */