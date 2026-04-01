/**
 * @file components/drivers/actuators/buzzer_driver/buzzer_driver.c
 * @brief Buzzer Driver – implementation using LEDC.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses LEDC with 10‑bit resolution (0..1023) for smooth duty control.
 * - Each buzzer gets its own LEDC channel and a dedicated timer (so frequencies
 *   can be changed independently without affecting others).
 * - The timer is configured only once per instance; subsequent frequency
 *   changes reconfigure the timer (which is allowed because it's not shared).
 * =============================================================================
 */

#include "buzzer_driver.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "BUZZER_DRV";

#define LEDC_RESOLUTION_BITS   10
#define LEDC_RESOLUTION_STEPS  (1 << LEDC_RESOLUTION_BITS)

struct buzzer_handle_t {
    ledc_channel_t channel;
    ledc_timer_t timer;
    gpio_num_t gpio_num;
    bool initialized;
    uint32_t current_freq;
    uint8_t current_duty;
};

/* Helper: convert duty percent to LEDC duty */
static uint32_t percent_to_duty(uint8_t percent)
{
    uint32_t duty = (uint32_t)percent * LEDC_RESOLUTION_STEPS / 100;
    if (duty > LEDC_RESOLUTION_STEPS - 1) duty = LEDC_RESOLUTION_STEPS - 1;
    return duty;
}

esp_err_t buzzer_driver_create(const buzzer_config_t *cfg, buzzer_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    buzzer_handle_t handle = calloc(1, sizeof(struct buzzer_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->gpio_num = cfg->gpio_num;
    handle->channel = cfg->channel;
    handle->timer = cfg->timer;
    handle->initialized = false;
    handle->current_freq = 0;
    handle->current_duty = 0;

    /* Configure the timer for this buzzer */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_RESOLUTION_BITS,
        .timer_num = handle->timer,
        .freq_hz = cfg->default_freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }

    /* Configure the channel */
    ledc_channel_config_t ch_cfg = {
        .gpio_num = handle->gpio_num,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = handle->channel,
        .timer_sel = handle->timer,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }

    handle->initialized = true;
    *out_handle = handle;
    ESP_LOGI(TAG, "Buzzer driver initialised (GPIO %d, channel %d, timer %d)",
             handle->gpio_num, handle->channel, handle->timer);
    return ESP_OK;
}

esp_err_t buzzer_driver_start_tone(buzzer_handle_t handle, uint32_t freq_hz, uint8_t duty_percent)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (freq_hz == 0) return ESP_ERR_INVALID_ARG;
    if (duty_percent > 100) duty_percent = 100;

    /* If frequency changed, reconfigure the timer */
    if (freq_hz != handle->current_freq) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_RESOLUTION_BITS,
            .timer_num = handle->timer,
            .freq_hz = freq_hz,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        esp_err_t ret = ledc_timer_config(&timer_cfg);
        if (ret != ESP_OK) return ret;
        handle->current_freq = freq_hz;
    }

    /* Set duty cycle */
    uint32_t duty = percent_to_duty(duty_percent);
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, duty);
    if (ret != ESP_OK) return ret;
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    if (ret != ESP_OK) return ret;
    handle->current_duty = duty_percent;

    return ESP_OK;
}

esp_err_t buzzer_driver_stop(buzzer_handle_t handle)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;

    /* Set duty to 0 (off) */
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, 0);
    if (ret != ESP_OK) return ret;
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    handle->current_duty = 0;
    return ret;
}

esp_err_t buzzer_driver_delete(buzzer_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    /* Stop the buzzer */
    buzzer_driver_stop(handle);

    /* Optionally, reset the GPIO to default state */
    gpio_reset_pin(handle->gpio_num);
    free(handle);
    ESP_LOGI(TAG, "Buzzer driver deleted");
    return ESP_OK;
}