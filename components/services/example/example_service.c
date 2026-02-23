/**
 * @file example_service.c
 * @brief Example service implementation.
 *
 * =============================================================================
 * This file demonstrates:
 *   - Using service_interfaces.h wrappers
 *   - Registering a command handler
 *   - Posting an event (simulated)
 *   - No business logic, no hardware access
 * =============================================================================
 */

#include "example_service.h"
#include "service_interfaces.h"
#include "esp_log.h"

static const char *TAG = "EXAMPLE_SVC";

/* ============================================================
 * EXAMPLE COMMAND HANDLER
 * ============================================================ */

/**
 * @brief Handler for CMD_NONE (example only).
 * 
 * This handler does nothing but return success.
 * It demonstrates the correct signature and parameter handling.
 * 
 * @param context Service context (unused here)
 * @param params  Command parameters (unused here)
 * @return ESP_OK
 */
static esp_err_t example_handle_none(void *context, void *params)
{
    (void)context;
    (void)params;
    ESP_LOGD(TAG, "CMD_NONE received (example)");
    return ESP_OK;
}

/* ============================================================
 * EXAMPLE EVENT EMISSION (simulated)
 *
 * This function is not part of the base contract; it shows how
 * a service might internally trigger an event (e.g., from a timer
 * or ISR callback). Here it is called manually for illustration.
 * ============================================================ */

static void example_simulate_event(void)
{
    system_event_t ev = {
        .id = EVENT_PERSON_DETECTED,
        .data = { {0} }   // zero payload /**<TODO: to be reviewed (this line) */
    };
    esp_err_t ret = service_post_event(&ev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event: %d", ret);
    } else {
        ESP_LOGD(TAG, "Simulated EVENT_PERSON_DETECTED posted");
    }
}

/* ============================================================
 * BASE CONTRACT IMPLEMENTATION
 * ============================================================ */

esp_err_t example_service_init(void)
{
    ESP_LOGI(TAG, "Example service initialised");
    return ESP_OK;
}

esp_err_t example_service_start(void)
{
    ESP_LOGI(TAG, "Example service started");
    // In a real service, you might start a timer here.
    // For this example, we immediately simulate an event.
    example_simulate_event();
    return ESP_OK;
}

esp_err_t example_service_stop(void)
{
    ESP_LOGI(TAG, "Example service stopped");
    return ESP_OK;
}

esp_err_t example_service_register_handlers(void)
{
    ESP_LOGI(TAG, "Registering example command handlers");

    esp_err_t ret = service_register_command(
        CMD_NONE,
        example_handle_none,
        NULL    // no context needed
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register CMD_NONE handler: %d", ret);
        return ret;
    }

    return ESP_OK;
}