/**
 * @file buzzer_driver.c
 * @brief Buzzer driver implementation using LEDC.
 */

#include "buzzer_driver.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "buzzer_driver";

/* GPIO pin for buzzer */
#define BUZZER_GPIO_PIN     18

/* LEDC timer and channel configuration */
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT    /* 10-bit resolution (0-1023) */
#define LEDC_FREQUENCY_HZ   2000                  /* Default 2 kHz */

static uint32_t s_current_freq = LEDC_FREQUENCY_HZ;
static bool s_initialised = false;

esp_err_t buzzer_driver_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }

    /* Configure LEDC timer */
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure LEDC channel */
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = BUZZER_GPIO_PIN,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .duty           = 0,                     /* initially off */
        .hpoint         = 0
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialised = true;
    s_current_freq = LEDC_FREQUENCY_HZ;
    ESP_LOGI(TAG, "Buzzer driver initialised on GPIO %d, %d Hz", BUZZER_GPIO_PIN, LEDC_FREQUENCY_HZ);
    return ESP_OK;
}

void buzzer_set(bool on)
{
    if (!s_initialised) return;

    uint32_t duty = on ? (1 << LEDC_DUTY_RES) / 2 : 0;  /* 50% duty when on */
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

esp_err_t buzzer_set_frequency(uint32_t freq_hz)
{
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    /* LEDC timer frequency change (will affect duty cycle scaling automatically) */
    esp_err_t ret = ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
    if (ret == ESP_OK) {
        s_current_freq = freq_hz;
        /* Re-apply current duty if buzzer is on? The duty cycle value remains the same,
           but the actual duty percentage stays 50% because LEDC scales duty with frequency
           change automatically. So no need to reset duty. */
        ESP_LOGD(TAG, "Frequency changed to %"PRIu32" Hz", freq_hz);
    } else {
        ESP_LOGE(TAG, "Failed to set frequency %"PRIu32" Hz: %s", freq_hz, esp_err_to_name(ret));
    }
    return ret;
}