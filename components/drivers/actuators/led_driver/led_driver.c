#include "led_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>

static const char *TAG = "LED_DRV";

#define LEDC_RESOLUTION_BITS   10
#define LEDC_RESOLUTION_STEPS  (1 << LEDC_RESOLUTION_BITS)

struct led_handle_t {
    ledc_channel_t channel;
    ledc_timer_t timer;
    gpio_num_t gpio_num;
    uint32_t freq_hz;
    bool active_high;
    uint8_t current_brightness;      /* 0‑100 */
    uint8_t steady_brightness;       /* last non‑blink brightness */
    bool blinking;                    /* true if blinking active */
    bool fading;                      /* true if fade in progress */
    esp_timer_handle_t blink_timer;
    esp_timer_handle_t fade_timer;
    /* Blink parameters */
    uint32_t blink_on_ms;
    uint32_t blink_off_ms;
    bool blink_state;                 /* true = LED on during this phase */
    /* Fade parameters */
    struct {
        uint8_t start;
        uint8_t target;
        uint32_t duration_ms;
        int64_t start_time_us;
    } fade;
};

/* Helper: convert brightness percent to duty cycle */
static uint32_t percent_to_duty(uint8_t percent)
{
    uint32_t duty = (uint32_t)percent * LEDC_RESOLUTION_STEPS / 100;
    if (duty > LEDC_RESOLUTION_STEPS - 1) duty = LEDC_RESOLUTION_STEPS - 1;
    return duty;
}

/* Helper: set LEDC duty */
static esp_err_t set_duty(led_handle_t handle, uint8_t percent)
{
    uint32_t duty = percent_to_duty(percent);
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, duty);
    if (ret != ESP_OK) return ret;
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    return ret;
}

/* Helper: apply active‑high inversion to stored brightness */
static uint8_t apply_active_high(led_handle_t handle, uint8_t percent)
{
    if (handle->active_high) return percent;
    return 100 - percent;
}

/* Blink timer callback */
static void blink_timer_callback(void *arg)
{
    led_handle_t handle = (led_handle_t)arg;
    if (!handle->blinking) return;

    if (handle->blink_state) {
        /* Current phase is on -> switch to off */
        set_duty(handle, apply_active_high(handle, 0));
        handle->current_brightness = 0;
        handle->blink_state = false;
        /* Re-arm timer with off duration */
        esp_timer_start_once(handle->blink_timer, handle->blink_off_ms * 1000);
    } else {
        /* Current phase is off -> switch to on (steady brightness) */
        set_duty(handle, apply_active_high(handle, handle->steady_brightness));
        handle->current_brightness = handle->steady_brightness;
        handle->blink_state = true;
        /* Re-arm timer with on duration */
        esp_timer_start_once(handle->blink_timer, handle->blink_on_ms * 1000);
    }
}

/* Fade timer callback – steps brightness */
static void fade_timer_callback(void *arg)
{
    led_handle_t handle = (led_handle_t)arg;
    if (!handle->fading) return;

    int64_t now = esp_timer_get_time();
    int64_t elapsed = now - handle->fade.start_time_us;
    if (elapsed >= handle->fade.duration_ms * 1000) {
        /* Fade complete */
        set_duty(handle, apply_active_high(handle, handle->fade.target));
        handle->current_brightness = handle->fade.target;
        handle->fading = false;
        esp_timer_stop(handle->fade_timer);
        return;
    }

    /* Linear interpolation: brightness = start + (target - start) * (elapsed / duration) */
    int64_t progress = elapsed * 1000 / (handle->fade.duration_ms * 1000); /* 0..1000 */
    uint8_t new_brightness = handle->fade.start +
                             (handle->fade.target - handle->fade.start) * progress / 1000;
    set_duty(handle, apply_active_high(handle, new_brightness));
    handle->current_brightness = new_brightness;
    /* The timer is periodic, so it will continue until stopped */
}

/* Public API */
esp_err_t led_driver_create(const led_config_t *cfg, led_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    led_handle_t handle = calloc(1, sizeof(struct led_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->gpio_num = cfg->gpio_num;
    handle->channel = cfg->channel;
    handle->timer = cfg->timer;
    handle->freq_hz = cfg->freq_hz;
    handle->active_high = cfg->active_high;
    handle->current_brightness = 0;
    handle->steady_brightness = 0;
    handle->blinking = false;
    handle->fading = false;

    /* Configure LEDC timer (shared among channels with same frequency) */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_RESOLUTION_BITS,
        .timer_num = handle->timer,
        .freq_hz = handle->freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        free(handle);
        return ret;
    }

    /* Configure LEDC channel */
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

    /* Create blink timer */
    esp_timer_create_args_t blink_args = {
        .callback = blink_timer_callback,
        .arg = handle,
        .name = "led_blink"
    };
    ret = esp_timer_create(&blink_args, &handle->blink_timer);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }

    /* Create fade timer */
    esp_timer_create_args_t fade_args = {
        .callback = fade_timer_callback,
        .arg = handle,
        .name = "led_fade"
    };
    ret = esp_timer_create(&fade_args, &handle->fade_timer);
    if (ret != ESP_OK) {
        esp_timer_delete(handle->blink_timer);
        free(handle);
        return ret;
    }

    /* Start with LED off */
    led_driver_off(handle);

    *out_handle = handle;
    ESP_LOGI(TAG, "LED driver created (GPIO %d, channel %d)", handle->gpio_num, handle->channel);
    return ESP_OK;
}

esp_err_t led_driver_set_brightness(led_handle_t handle, uint8_t percent)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (percent > 100) percent = 100;

    /* Stop any ongoing blink/fade */
    if (handle->blinking) led_driver_stop_blink(handle);
    if (handle->fading) led_driver_stop_fade(handle);

    esp_err_t ret = set_duty(handle, apply_active_high(handle, percent));
    if (ret == ESP_OK) {
        handle->current_brightness = percent;
        handle->steady_brightness = percent;
    }
    return ret;
}

esp_err_t led_driver_on(led_handle_t handle)
{
    return led_driver_set_brightness(handle, 100);
}

esp_err_t led_driver_off(led_handle_t handle)
{
    return led_driver_set_brightness(handle, 0);
}

esp_err_t led_driver_toggle(led_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    uint8_t new_brightness = (handle->current_brightness > 0) ? 0 : 100;
    return led_driver_set_brightness(handle, new_brightness);
}

esp_err_t led_driver_start_blink(led_handle_t handle, uint32_t period_ms, uint8_t duty_percent)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (period_ms == 0) return ESP_ERR_INVALID_ARG;
    if (duty_percent > 100) duty_percent = 100;

    /* Stop any ongoing blink/fade */
    if (handle->blinking) led_driver_stop_blink(handle);
    if (handle->fading) led_driver_stop_fade(handle);

    /* Store steady brightness before blink */
    handle->steady_brightness = handle->current_brightness;

    /* Calculate on and off times */
    uint32_t on_ms = (period_ms * duty_percent) / 100;
    uint32_t off_ms = period_ms - on_ms;
    if (on_ms == 0 || off_ms == 0) {
        /* If duty is 0 or 100, just set brightness directly */
        return led_driver_set_brightness(handle, duty_percent ? 100 : 0);
    }

    handle->blink_on_ms = on_ms;
    handle->blink_off_ms = off_ms;
    handle->blink_state = true;  /* start with on */
    handle->blinking = true;

    /* Set LED to steady brightness (on) */
    set_duty(handle, apply_active_high(handle, handle->steady_brightness));
    handle->current_brightness = handle->steady_brightness;

    /* Start the timer with on duration */
    esp_timer_start_once(handle->blink_timer, on_ms * 1000);

    return ESP_OK;
}

esp_err_t led_driver_stop_blink(led_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (!handle->blinking) return ESP_OK;

    esp_timer_stop(handle->blink_timer);
    handle->blinking = false;
    /* Restore steady brightness */
    return led_driver_set_brightness(handle, handle->steady_brightness);
}

esp_err_t led_driver_start_fade(led_handle_t handle, uint8_t target_percent, uint32_t duration_ms)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (duration_ms == 0) return ESP_ERR_INVALID_ARG;
    if (target_percent > 100) target_percent = 100;

    /* Stop any ongoing blink/fade */
    if (handle->blinking) led_driver_stop_blink(handle);
    if (handle->fading) led_driver_stop_fade(handle);

    uint8_t start = handle->current_brightness;
    if (start == target_percent) {
        /* Already at target */
        return ESP_OK;
    }

    handle->fade.start = start;
    handle->fade.target = target_percent;
    handle->fade.duration_ms = duration_ms;
    handle->fade.start_time_us = esp_timer_get_time();
    handle->fading = true;

    /* Start periodic timer with 10 ms step (adjustable) */
    /* We'll use a 10 ms interval for smoothness */
    esp_timer_start_periodic(handle->fade_timer, 10000); /* 10 ms */

    return ESP_OK;
}

esp_err_t led_driver_stop_fade(led_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (!handle->fading) return ESP_OK;

    esp_timer_stop(handle->fade_timer);
    handle->fading = false;
    return ESP_OK;
}

esp_err_t led_driver_delete(led_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    if (handle->blinking) led_driver_stop_blink(handle);
    if (handle->fading) led_driver_stop_fade(handle);

    esp_timer_delete(handle->blink_timer);
    esp_timer_delete(handle->fade_timer);
    gpio_reset_pin(handle->gpio_num);
    free(handle);
    ESP_LOGI(TAG, "LED driver deleted");
    return ESP_OK;
}