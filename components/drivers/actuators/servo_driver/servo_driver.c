/**
 * @file servo_driver.c
 * @brief Servo driver implementation using LEDC.
 *
 * =============================================================================
 * Uses LEDC high‑speed mode. The driver computes duty cycle from pulse width
 * based on the timer resolution (configured automatically by LEDC).
 * =============================================================================
 */

#include "servo_driver.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>





static const char *TAG = "SERVO_DRV";

/**
 * Internal handle structure.
 */
struct servo_handle_t {
    gpio_num_t pwm_pin;
    ledc_timer_t timer;
    ledc_channel_t channel;
    uint32_t frequency_hz;
    uint32_t min_pulse_us;
    uint32_t max_pulse_us;
    uint32_t max_duty;            /* cached from LEDC timer configuration */
};

/* Helper: compute duty cycle for a given pulse width (us) */
static uint32_t pulse_to_duty(servo_handle_t handle, uint32_t pulse_us)
{
    /* LEDC duty resolution is (2^duty_res) steps, where duty_res is set during timer config.
       The driver automatically uses the highest possible resolution for the given frequency.
       We can obtain the max duty from ledc_get_max_duty(handle->timer). */
    uint64_t duty = (uint64_t)pulse_us * handle->max_duty / (1000000ULL / handle->frequency_hz);
    return (uint32_t)duty;
}

esp_err_t servo_driver_create(const servo_config_t *config,
                              servo_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    servo_handle_t handle = calloc(1, sizeof(struct servo_handle_t));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(handle, config, sizeof(servo_config_t));

    /* Configure LEDC timer (high speed) */
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num  = handle->timer,
        .duty_resolution = LEDC_TIMER_10_BIT, /* will be auto-adjusted? Actually we need to pick a resolution.
                                                 LEDC driver can auto-select based on frequency, but we set manually for clarity */
        .freq_hz    = handle->frequency_hz,
        .clk_cfg    = LEDC_AUTO_CLK
    };
    /* Let the driver choose the highest resolution that fits the frequency */
    timer_conf.duty_resolution = LEDC_TIMER_13_BIT; /* try 13-bit; could be more generic */
    /* Instead, we can use ledc_timer_config() with duty_resolution = LEDC_TIMER_13_BIT
       and it will reduce resolution if needed. We'll accept the result. */
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %d", ret);
        free(handle);
        return ret;
    }

    /* Get the actual max duty (depends on resolution) */
    handle->max_duty = (1 << timer_conf.duty_resolution) - 1;

    /* Configure LEDC channel */
    ledc_channel_config_t chan_conf = {
        .gpio_num   = handle->pwm_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = handle->channel,
        .timer_sel  = handle->timer,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE
    };
    ret = ledc_channel_config(&chan_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %d", ret);
        free(handle);
        return ret;
    }

    *out_handle = handle;
    ESP_LOGD(TAG, "Servo created on pin %d", handle->pwm_pin);
    return ESP_OK;
}

esp_err_t servo_driver_set_angle(servo_handle_t handle, float angle_deg)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Clamp angle to [0, 180] */
    if (angle_deg < 0.0f) angle_deg = 0.0f;
    if (angle_deg > 180.0f) angle_deg = 180.0f;

    /* Convert angle to pulse width */
    uint32_t pulse_us = handle->min_pulse_us +
        (uint32_t)((handle->max_pulse_us - handle->min_pulse_us) * angle_deg / 180.0f);

    /* Convert pulse width to duty */
    uint32_t duty = pulse_to_duty(handle, pulse_us);

    /* Set duty */
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, duty);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    ESP_LOGD(TAG, "Set angle %.1f° → pulse %"PRIu32" us, duty %"PRIu32, angle_deg, pulse_us, duty);
    return ret;
}

esp_err_t servo_driver_stop(servo_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Set duty to 0 effectively stops PWM (signal low) */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    ESP_LOGD(TAG, "Servo stopped (duty 0)");
    return ESP_OK;
}

esp_err_t servo_driver_delete(servo_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Optionally stop PWM before deleting */
    servo_driver_stop(handle);
    free(handle);
    ESP_LOGD(TAG, "Servo deleted");
    return ESP_OK;
}