/**
 * @file ultrasonic_driver.c
 * @brief Ultrasonic driver implementation.
 *
 * =============================================================================
 * Uses esp_timer_get_time() for microsecond precision.
 * Trigger pulse: 10µs high on trig pin.
 * Echo measurement: measures high pulse width on echo pin.
 * Timeout handled via busy-wait with esp_timer.
 * =============================================================================
 */

#include "ultrasonic_driver.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ULTRASONIC_DRV";

#define TRIGGER_PULSE_US 10

/**
 * Internal handle structure.
 */
struct ultrasonic_handle_t {
    gpio_num_t trig_pin;
    gpio_num_t echo_pin;
    uint32_t timeout_us;
};

esp_err_t ultrasonic_driver_create(const ultrasonic_config_t *cfg,
                                   ultrasonic_handle_t *out_handle)
{
    if (cfg == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate handle
    struct ultrasonic_handle_t *handle = calloc(1, sizeof(struct ultrasonic_handle_t));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->trig_pin = cfg->trig_pin;
    handle->echo_pin = cfg->echo_pin;
    handle->timeout_us = cfg->timeout_us;

    // Configure GPIOs
    gpio_config_t trig_conf = {
        .pin_bit_mask   = (1ULL << handle->trig_pin),
        .mode           = GPIO_MODE_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&trig_conf);

    gpio_config_t echo_conf = {
        .pin_bit_mask = (1ULL << handle->echo_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&echo_conf);

    // Ensure trigger is low
    gpio_set_level(handle->trig_pin, 0);

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t ultrasonic_driver_measure(ultrasonic_handle_t handle,
                                    uint32_t *pulse_width_us)
{
    if (handle == NULL || pulse_width_us == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Send trigger pulse: 10µs high
    gpio_set_level(handle->trig_pin, 1);
    esp_rom_delay_us(TRIGGER_PULSE_US);
    gpio_set_level(handle->trig_pin, 0);

    // Wait for echo pin to go high (start of pulse)
    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(handle->echo_pin) == 0) {
        if ((esp_timer_get_time() - start_time) > handle->timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    // Measure pulse width
    int64_t pulse_start = esp_timer_get_time();
    while (gpio_get_level(handle->echo_pin) == 1) {
        if ((esp_timer_get_time() - pulse_start) > handle->timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }
    int64_t pulse_end = esp_timer_get_time();

    *pulse_width_us = (uint32_t)(pulse_end - pulse_start);
    return ESP_OK;
}

esp_err_t ultrasonic_driver_delete(ultrasonic_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    free(handle);
    return ESP_OK;
}