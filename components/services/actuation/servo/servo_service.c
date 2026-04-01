/**
 * @file components/services/actuation/servo_service/servo_service.c
 * @brief Servo Service – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses the servo driver for hardware control.
 * - Handles CMD_OPEN_LID, CMD_CLOSE_LID, CMD_SERVO_SET_ANGLE, CMD_SERVO_STOP.
 * - Posts EVENT_LID_OPENED or EVENT_LID_CLOSED after a fixed delay (simulating
 *   movement time). A FreeRTOS timer is used for this delay.
 * - If a command fails, posts EVENT_SERVO_ERROR.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-03-30
 */

#include "servo_service.h"
#include "servo_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "SERVO_SVC";

/* Servo configuration – adjust according to your hardware */
#define SERVO_GPIO           GPIO_NUM_14
#define SERVO_CHANNEL        LEDC_CHANNEL_0
#define SERVO_TIMER          LEDC_TIMER_0
#define SERVO_MIN_PULSE_US   500    /* 0° pulse width */
#define SERVO_MAX_PULSE_US   2500   /* 180° pulse width */
#define SERVO_FREQ_HZ        50

/* Movement time in milliseconds (estimated) */
#define SERVO_MOVE_TIME_MS   500

static servo_handle_t s_servo = NULL;
static TimerHandle_t s_move_timer = NULL;
static bool s_moving = false;
static bool s_last_cmd_was_open = false;    /* Defining this variable in file scope */

/* Forward declaration */
static void move_complete_callback(TimerHandle_t xTimer);

/* Helper to post event after movement */
static void post_movement_event(bool opened)
{
    system_event_id_t ev_id = opened ? EVENT_LID_OPENED : EVENT_LID_CLOSED;
    system_event_t ev = {
        .id = ev_id,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    service_post_event(&ev);
    ESP_LOGI(TAG, "%s", opened ? "Lid opened" : "Lid closed");
}

/* Command handlers */
static esp_err_t cmd_open_lid(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!s_servo) return ESP_ERR_INVALID_STATE;
    if (s_moving) return ESP_ERR_INVALID_STATE; /* already moving */

    esp_err_t ret = servo_driver_set_angle(s_servo, 90.0f); /* assume 90° is open */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open lid: %d", ret);
        system_event_t ev = { .id = EVENT_SERVO_ERROR, .timestamp_us = esp_timer_get_time(), .source = 0, .data = { { {0} } } };
        service_post_event(&ev);
        return ret;
    }

    s_moving = true;
    /* Start timer to simulate movement completion */
    xTimerReset(s_move_timer, 0);
    return ESP_OK;
}

static esp_err_t cmd_close_lid(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!s_servo) return ESP_ERR_INVALID_STATE;
    if (s_moving) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = servo_driver_set_angle(s_servo, 0.0f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to close lid: %d", ret);
        system_event_t ev = { .id = EVENT_SERVO_ERROR, .timestamp_us = esp_timer_get_time(), .source = 0, .data = { { {0} } } };
        service_post_event(&ev);
        return ret;
    }

    s_moving = true;
    xTimerReset(s_move_timer, 0);
    return ESP_OK;
}

static esp_err_t cmd_servo_set_angle(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    if (!s_servo) return ESP_ERR_INVALID_STATE;
    if (s_moving) return ESP_ERR_INVALID_STATE;

    const servo_command_data_t *p = &params->servo;
    esp_err_t ret = servo_driver_set_angle(s_servo, p->angle_deg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set angle: %d", ret);
        system_event_t ev = { .id = EVENT_SERVO_ERROR, .timestamp_us = esp_timer_get_time(), .source = 0, .data = { { {0} } } };
        service_post_event(&ev);
        return ret;
    }

    /* For generic set_angle, we don't know if it's open or close; we could
       optionally post a generic EVENT_SERVO_MOVEMENT_COMPLETED, but for now
       we just don't post any event. */
    return ESP_OK;
}

static esp_err_t cmd_servo_stop(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!s_servo) return ESP_ERR_INVALID_STATE;

    /* Stop the PWM and cancel any pending movement timer */
    servo_driver_stop(s_servo);
    if (s_move_timer) {
        xTimerStop(s_move_timer, 0);
    }
    s_moving = false;
    return ESP_OK;
}

/* Timer callback – movement completed */
static void move_complete_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    s_moving = false;
    /* We need to know whether it was open or close to post correct event.
       Since the timer is started by both open and close commands, we need to
       remember the last commanded action. Simpler: we can check the current
       angle? Not reliable. Instead, we can pass context to the timer.
       To keep it simple, we'll use a variable to store the pending event.
       We'll create a separate variable for the pending event type. */
    static bool last_was_open = false; /* This is not thread-safe but OK as only one command at a time. */
    /* Actually, we'll set a variable in the command handlers and read it here.
       Let's add a static variable `s_last_cmd_was_open`. */
    // extern bool s_last_cmd_was_open; /* we'll define it in file scope */
    if (s_last_cmd_was_open) {
        post_movement_event(true);
    } else {
        post_movement_event(false);
    }
}

/* Updated command handlers to set the flag */
static esp_err_t cmd_open_lid_fixed(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!s_servo) return ESP_ERR_INVALID_STATE;
    if (s_moving) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = servo_driver_set_angle(s_servo, 90.0f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open lid: %d", ret);
        system_event_t ev = { .id = EVENT_SERVO_ERROR, .timestamp_us = esp_timer_get_time(), .source = 0, .data = { { {0} } } };
        service_post_event(&ev);
        return ret;
    }

    s_moving = true;
    s_last_cmd_was_open = true;
    xTimerReset(s_move_timer, 0);
    return ESP_OK;
}

static esp_err_t cmd_close_lid_fixed(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!s_servo) return ESP_ERR_INVALID_STATE;
    if (s_moving) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = servo_driver_set_angle(s_servo, 0.0f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to close lid: %d", ret);
        system_event_t ev = { .id = EVENT_SERVO_ERROR, .timestamp_us = esp_timer_get_time(), .source = 0, .data = { { {0} } } };
        service_post_event(&ev);
        return ret;
    }

    s_moving = true;
    s_last_cmd_was_open = false;
    xTimerReset(s_move_timer, 0);
    return ESP_OK;
}

/* Register handlers – use the fixed versions */
static esp_err_t cmd_open_lid_wrapper(void *context, void *params)
{
    return cmd_open_lid_fixed(context, (const command_param_union_t*)params);
}
static esp_err_t cmd_close_lid_wrapper(void *context, void *params)
{
    return cmd_close_lid_fixed(context, (const command_param_union_t*)params);
}
static esp_err_t cmd_servo_set_angle_wrapper(void *context, void *params)
{
    return cmd_servo_set_angle(context, (const command_param_union_t*)params);
}
static esp_err_t cmd_servo_stop_wrapper(void *context, void *params)
{
    return cmd_servo_stop(context, (const command_param_union_t*)params);
}

/* Public API */
esp_err_t servo_service_init(void)
{
    servo_config_t cfg = {
        .gpio_num = SERVO_GPIO,
        .channel = SERVO_CHANNEL,
        .timer = SERVO_TIMER,
        .min_pulse_us = SERVO_MIN_PULSE_US,
        .max_pulse_us = SERVO_MAX_PULSE_US,
        .freq_hz = SERVO_FREQ_HZ,
    };
    esp_err_t ret = servo_driver_create(&cfg, &s_servo);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create servo driver: %d", ret);
        return ret;
    }

    /* Create movement completion timer */
    s_move_timer = xTimerCreate("servo_move", pdMS_TO_TICKS(SERVO_MOVE_TIME_MS), pdFALSE, NULL, move_complete_callback);
    if (!s_move_timer) {
        ESP_LOGE(TAG, "Failed to create movement timer");
        servo_driver_delete(s_servo);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Servo service initialised");
    return ESP_OK;
}

esp_err_t servo_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_OPEN_LID, cmd_open_lid_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_CLOSE_LID, cmd_close_lid_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_SERVO_SET_ANGLE, cmd_servo_set_angle_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_SERVO_STOP, cmd_servo_stop_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Servo command handlers registered");
    return ESP_OK;
}

esp_err_t servo_service_start(void)
{
    /* Nothing to start; timers are ready */
    ESP_LOGI(TAG, "Servo service started");
    return ESP_OK;
}

esp_err_t servo_service_stop(void)
{
    if (s_move_timer) {
        xTimerDelete(s_move_timer, 0);
        s_move_timer = NULL;
    }
    if (s_servo) {
        servo_driver_stop(s_servo);
        servo_driver_delete(s_servo);
        s_servo = NULL;
    }
    ESP_LOGI(TAG, "Servo service stopped");
    return ESP_OK;
}