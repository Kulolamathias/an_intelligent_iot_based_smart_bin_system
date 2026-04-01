/**
 * @file components/drivers/actuators/servo_driver/servo_driver.c
 * @brief Servo Driver – implementation using LEDC.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses LEDC with 10‑bit resolution (0..1023) and 50 Hz PWM frequency.
 * - Each servo uses its own LEDC channel and can share a timer if the same
 *   frequency is used (which is true for standard servos).
 * - The `stop` function sets the duty cycle to 0, disabling the PWM signal
 *   and allowing the servo to relax.
 * - Multiple instances are supported by storing channel and timer per handle.
 * =============================================================================
 */

#include "servo_driver.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "SERVO_DRV";

/* LEDC resolution (10 bits = 1024 steps) */
#define LEDC_RESOLUTION_BITS   10
#define LEDC_RESOLUTION_STEPS  (1 << LEDC_RESOLUTION_BITS)

/* Default frequency for servos (50 Hz) */
#define DEFAULT_FREQ_HZ        50

/* Internal handle structure */
struct servo_handle_t {
    ledc_channel_t channel;
    ledc_timer_t timer;
    gpio_num_t gpio_num;
    uint32_t min_pulse_us;
    uint32_t max_pulse_us;
    uint32_t freq_hz;
    bool initialized;
};

/* Helper: convert angle to duty cycle (0..resolution-1) */
static uint32_t angle_to_duty(servo_handle_t handle, float angle_deg)
{
    /* Clamp angle to 0..180 */
    if (angle_deg < 0) angle_deg = 0;
    if (angle_deg > 180) angle_deg = 180;

    /* Linear interpolation between min and max pulse widths */
    uint32_t pulse_us = handle->min_pulse_us +
                        (uint32_t)((handle->max_pulse_us - handle->min_pulse_us) * angle_deg / 180.0f);

    /* Period in microseconds = 1,000,000 / freq_hz */
    uint32_t period_us = 1000000 / handle->freq_hz;
    uint32_t duty = (pulse_us * LEDC_RESOLUTION_STEPS) / period_us;
    if (duty >= LEDC_RESOLUTION_STEPS) duty = LEDC_RESOLUTION_STEPS - 1;
    return duty;
}

esp_err_t servo_driver_create(const servo_config_t *cfg, servo_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    /* Allocate handle */
    servo_handle_t handle = calloc(1, sizeof(struct servo_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->gpio_num = cfg->gpio_num;
    handle->channel = cfg->channel;
    handle->timer = cfg->timer;
    handle->min_pulse_us = cfg->min_pulse_us;
    handle->max_pulse_us = cfg->max_pulse_us;
    handle->freq_hz = (cfg->freq_hz != 0) ? cfg->freq_hz : DEFAULT_FREQ_HZ;
    handle->initialized = false;

    /* Configure LEDC timer (only once per timer group; if timer already configured, this may fail.
       To keep it simple, we assume each servo uses its own timer or we ignore reconfig errors.
       In production, you'd want to check if timer is already configured. */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_RESOLUTION_BITS,
        .timer_num = handle->timer,
        .freq_hz = handle->freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Timer %d already configured (ignoring)", handle->timer);
        /* Not fatal, but continue */
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

    handle->initialized = true;
    *out_handle = handle;
    ESP_LOGI(TAG, "Servo created (GPIO %d, channel %d)", handle->gpio_num, handle->channel);
    return ESP_OK;
}

esp_err_t servo_driver_set_angle(servo_handle_t handle, float angle_deg)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;

    uint32_t duty = angle_to_duty(handle, angle_deg);
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, duty);
    if (ret != ESP_OK) return ret;
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    return ret;
}

esp_err_t servo_driver_stop(servo_handle_t handle)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;

    /* Set duty to 0 to stop PWM signal */
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, 0);
    if (ret != ESP_OK) return ret;
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    return ret;
}

esp_err_t servo_driver_delete(servo_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    /* Stop the servo first */
    servo_driver_stop(handle);

    /* Optionally, release the channel? LEDC doesn't have a delete function,
       but we can reset the channel configuration. */
    ledc_channel_config_t ch_cfg = {
        .gpio_num = handle->gpio_num,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = handle->channel,
        .timer_sel = handle->timer,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg); /* Reconfigure to default, but not necessary. */

    free(handle);
    ESP_LOGI(TAG, "Servo deleted");
    return ESP_OK;
}



#if 0  /**< RESERVED FOR gradual movement */

// Add these constants
#define SERVO_STEP_DELAY_MS  20      // 20 ms between steps
#define SERVO_STEP_DEG        5       // 5° per step

// Add a static variable for the current target angle (if you want to track)
static float s_current_angle = 0.0f;
static bool s_moving = false;
static float s_target_angle = 0.0f;
static TimerHandle_t s_ramp_timer = NULL;  // using esp_timer or FreeRTOS timer

// Timer callback to perform one step
static void ramp_step_callback(TimerHandle_t xTimer)
{
    if (!s_moving) return;
    float step = (s_target_angle > s_current_angle) ? SERVO_STEP_DEG : -SERVO_STEP_DEG;
    float new_angle = s_current_angle + step;
    if ((step > 0 && new_angle >= s_target_angle) || (step < 0 && new_angle <= s_target_angle)) {
        new_angle = s_target_angle;
        s_moving = false;
        xTimerStop(s_ramp_timer, 0);
    }
    servo_driver_set_angle(s_servo, new_angle);
    s_current_angle = new_angle;
    if (!s_moving) {
        // Movement complete – post lid opened/closed event
        system_event_t ev = {
            .id = (s_target_angle == 90.0f) ? EVENT_LID_OPENED : EVENT_LID_CLOSED,
            .timestamp_us = esp_timer_get_time(),
            .source = 0,
            .data = {0}
        };
        service_post_event(&ev);
    }
}

// Modified open/close handlers using ramp
static esp_err_t cmd_open_lid_ramp(void *context, void *params)
{
    (void)context; (void)params;
    if (s_moving) return ESP_ERR_INVALID_STATE;
    s_target_angle = 90.0f;
    s_moving = true;
    xTimerReset(s_ramp_timer, 0);
    return ESP_OK;
}

#endif