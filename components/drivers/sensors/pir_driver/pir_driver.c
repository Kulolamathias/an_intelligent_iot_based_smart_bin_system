/**
 * @file components/drivers/sensors/pir_driver/pir_driver.c
 * @brief PIR Motion Sensor Driver – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Configures the GPIO pin as input with pull‑down disabled (most PIR modules
 *   output a clean high/low signal; pull‑down is optional).
 * - The read function simply returns the digital level of the GPIO.
 * - Multiple instances are supported (each with its own GPIO and handle).
 * =============================================================================
 */

#include "pir_driver.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "PIR_DRV";

/* Internal handle structure */
struct pir_handle_t {
    gpio_num_t gpio_num;
    bool initialized;
};

esp_err_t pir_driver_create(const pir_config_t *cfg, pir_handle_t *out_handle)
{
    if (!cfg || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Allocate handle */
    pir_handle_t handle = calloc(1, sizeof(struct pir_handle_t));
    if (!handle) {
        return ESP_ERR_NO_MEM;
    }

    handle->gpio_num = cfg->gpio_num;
    handle->initialized = false;

    /* Configure GPIO as input */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << handle->gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }

    handle->initialized = true;
    *out_handle = handle;
    ESP_LOGI(TAG, "PIR driver initialised (GPIO %d)", handle->gpio_num);
    return ESP_OK;
}

esp_err_t pir_driver_read(pir_handle_t handle, bool *motion_detected)
{
    if (!handle || !handle->initialized || !motion_detected) {
        return ESP_ERR_INVALID_ARG;
    }

    int level = gpio_get_level(handle->gpio_num);
    *motion_detected = (level == 1);
    return ESP_OK;
}

esp_err_t pir_driver_delete(pir_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Reset the GPIO to default state (optional) */
    gpio_reset_pin(handle->gpio_num);
    free(handle);
    ESP_LOGI(TAG, "PIR driver deleted");
    return ESP_OK;
}