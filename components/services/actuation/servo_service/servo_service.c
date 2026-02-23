/**
 * @file servo_service.c
 * @brief Servo service implementation.
 *
 * =============================================================================
 * Manages up to SERVO_MAX servos (defined in core_types.h).
 * Each servo has its own configuration (pins, angles, speed).
 * A dedicated FreeRTOS task performs smooth stepping.
 *
 * Commands handled:
 *   - CMD_SERVO_SET_ANGLE – set target angle (triggers smooth move)
 *   - CMD_SERVO_STOP      – stop current movement
 *
 * Events emitted (hardware level):
 *   - EVENT_SERVO_MOVEMENT_STARTED
 *   - EVENT_SERVO_MOVEMENT_COMPLETED
 *   - EVENT_SERVO_ERROR
 *
 * No domain‑level lid logic – core interprets movement events.
 * =============================================================================
 */

#include "servo_service.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "servo_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

#define CONFIG_SERVO_LID_OPEN_ANGLE 60
#define CONFIG_SERVO_LID_CLOSE_ANGLE 60

/* Kconfig defaults – in real system these come from config service */
#define LID_OPEN_ANGLE          CONFIG_SERVO_LID_OPEN_ANGLE
#define LID_CLOSE_ANGLE         CONFIG_SERVO_LID_CLOSE_ANGLE
#define SERVO_SPEED_DEG_PER_SEC CONFIG_SERVO_SPEED_DEG_PER_SEC
#define SERVO_UPDATE_MS         20          /* task period in ms */
#define SERVO_TASK_STACK_SIZE   4096
#define SERVO_TASK_PRIORITY     5
#define CONFIG_SERVO_LID_PIN    3

static const char *TAG = "SERVO_SVC";

/* ============================================================
 * Per‑servo state structure
 * ============================================================ */
typedef struct {
    servo_handle_t  drv_handle;        /* driver handle */
    float           current_angle;      /* current actual angle */
    float           target_angle;       /* desired angle */
    bool            moving;             /* true if movement in progress */
    /* configuration (could be loaded from NVS) */
    float           open_angle;
    float           close_angle;
    float           speed_deg_per_sec;  /* movement speed */
} servo_state_t;

/* ============================================================
 * Static data
 * ============================================================ */
static servo_state_t s_servos[SERVO_MAX];
static TaskHandle_t s_motion_task = NULL;
static SemaphoreHandle_t s_servo_mutex = NULL;
static volatile bool s_task_running = false;

/* ============================================================
 * Forward declarations
 * ============================================================ */
static void motion_task(void *pvParameters);
static void update_servo(servo_state_t *sv);

/* ============================================================
 * Configuration (temporary – replace with config service later)
 * ============================================================ */
static const struct {
    gpio_num_t pwm_pin;
    ledc_timer_t timer;
    ledc_channel_t channel;
    float open_angle;
    float close_angle;
    float speed_deg_per_sec;
} s_servo_config[SERVO_MAX] = {
    [SERVO_LID] = {
        .pwm_pin = CONFIG_SERVO_LID_PIN,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
        .open_angle = 90.0f,
        .close_angle = 0.0f,
        .speed_deg_per_sec = 60.0f
    },
    [SERVO_AUX] = {
        .pwm_pin = GPIO_NUM_NC,   /* not used yet */
        .timer = LEDC_TIMER_1,
        .channel = LEDC_CHANNEL_1,
        .open_angle = 0.0f,
        .close_angle = 0.0f,
        .speed_deg_per_sec = 60.0f
    }
};

/* ============================================================
 * Helper: post servo event
 * ============================================================ */
static void post_servo_event(system_event_id_t ev_id, servo_id_t servo_id, float angle)
{
    system_event_t ev = {
        .id = ev_id,
        .data.servo_event = {
            .servo_id = servo_id,
            .final_angle = angle
        }
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "Posted event %d for servo %d", ev_id, servo_id);
}

/* ============================================================
 * Command handlers
 * ============================================================ */

static esp_err_t handle_servo_set_angle(void *context, void *params)
{
    (void)context;
    if (params == NULL) return ESP_ERR_INVALID_ARG;
    servo_command_data_t *cmd = (servo_command_data_t*)params;
    if (cmd->servo_id >= SERVO_MAX) return ESP_ERR_INVALID_ARG;

    servo_state_t *sv = &s_servos[cmd->servo_id];
    xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
    sv->target_angle = cmd->angle_deg;
    if (!sv->moving) {
        sv->moving = true;
        post_servo_event(EVENT_SERVO_MOVEMENT_STARTED, cmd->servo_id, sv->target_angle);
    }
    xSemaphoreGive(s_servo_mutex);
    return ESP_OK;
}

static esp_err_t handle_servo_stop(void *context, void *params)
{
    (void)context;
    if (params == NULL) return ESP_ERR_INVALID_ARG;
    servo_command_data_t *cmd = (servo_command_data_t*)params;
    if (cmd->servo_id >= SERVO_MAX) return ESP_ERR_INVALID_ARG;

    servo_state_t *sv = &s_servos[cmd->servo_id];
    xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
    sv->moving = false;
    xSemaphoreGive(s_servo_mutex);
    return ESP_OK;
}

static esp_err_t handle_servo_open(void *context, void *params)
{
    (void)context;
    if (params == NULL) return ESP_ERR_INVALID_ARG;
    servo_command_data_t *cmd = (servo_command_data_t*)params;
    if (cmd->servo_id >= SERVO_MAX) return ESP_ERR_INVALID_ARG;

    servo_state_t *sv = &s_servos[cmd->servo_id];
    xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
    sv->target_angle = sv->open_angle;
    if (!sv->moving) {
        sv->moving = true;
        post_servo_event(EVENT_SERVO_MOVEMENT_STARTED, cmd->servo_id, sv->target_angle);
    }
    xSemaphoreGive(s_servo_mutex);
    return ESP_OK;
}

static esp_err_t handle_servo_close(void *context, void *params)
{
    (void)context;
    if (params == NULL) return ESP_ERR_INVALID_ARG;
    servo_command_data_t *cmd = (servo_command_data_t*)params;
    if (cmd->servo_id >= SERVO_MAX) return ESP_ERR_INVALID_ARG;

    servo_state_t *sv = &s_servos[cmd->servo_id];
    xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
    sv->target_angle = sv->close_angle;
    if (!sv->moving) {
        sv->moving = true;
        post_servo_event(EVENT_SERVO_MOVEMENT_STARTED, cmd->servo_id, sv->target_angle);
    }
    xSemaphoreGive(s_servo_mutex);
    return ESP_OK;
}

/* ============================================================
 * Motion task
 * ============================================================ */
static void motion_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Servo motion task started");

    TickType_t last_wake = xTaskGetTickCount();

    while (s_task_running) {
        /* Process each servo */
        for (int i = 0; i < SERVO_MAX; i++) {
            servo_state_t *sv = &s_servos[i];
            if (sv->drv_handle == NULL) continue;

            xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
            if (sv->moving) {
                update_servo(sv);
            }
            xSemaphoreGive(s_servo_mutex);
        }

        /* Wait for next cycle */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SERVO_UPDATE_MS));
    }

    ESP_LOGI(TAG, "Servo motion task stopped");
    vTaskDelete(NULL);
}

/* Update a single servo: move one step towards target */
static void update_servo(servo_state_t *sv)
{
    float step = sv->speed_deg_per_sec * (SERVO_UPDATE_MS / 1000.0f);
    float error = sv->target_angle - sv->current_angle;

    if (fabsf(error) <= step) {
        /* reached target */
        sv->current_angle = sv->target_angle;
        sv->moving = false;
        servo_driver_set_angle(sv->drv_handle, sv->current_angle);
        post_servo_event(EVENT_SERVO_MOVEMENT_COMPLETED, 
                         (servo_id_t)(sv - s_servos), sv->current_angle);
    } else {
        /* move one step */
        if (error > 0) {
            sv->current_angle += step;
        } else {
            sv->current_angle -= step;
        }
        servo_driver_set_angle(sv->drv_handle, sv->current_angle);
    }
}

/* ============================================================
 * Base contract implementation
 * ============================================================ */

esp_err_t servo_service_init(void)
{
    esp_err_t ret;

    /* Create mutex */
    s_servo_mutex = xSemaphoreCreateMutex();
    if (s_servo_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    /* Initialise each servo */
    memset(s_servos, 0, sizeof(s_servos));
    for (int i = 0; i < SERVO_MAX; i++) {
        if (s_servo_config[i].pwm_pin == GPIO_NUM_NC) continue;

        servo_config_t drv_cfg = {
            .pwm_pin = s_servo_config[i].pwm_pin,
            .timer = s_servo_config[i].timer,
            .channel = s_servo_config[i].channel,
            .pwm_frequency_hz = 50,
            .min_pulse_width_us = 500,
            .max_pulse_width_us = 2500
        };
        ret = servo_driver_create(&drv_cfg, &s_servos[i].drv_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create servo %d driver", i);
            return ret;
        }

        s_servos[i].open_angle = s_servo_config[i].open_angle;
        s_servos[i].close_angle = s_servo_config[i].close_angle;
        s_servos[i].speed_deg_per_sec = s_servo_config[i].speed_deg_per_sec;
        s_servos[i].current_angle = s_servos[i].close_angle; /* start closed */
        s_servos[i].target_angle = s_servos[i].close_angle;
        s_servos[i].moving = false;

        /* Set initial position */
        servo_driver_set_angle(s_servos[i].drv_handle, s_servos[i].current_angle);
        ESP_LOGI(TAG, "Servo %d initialised at %.1f°", i, s_servos[i].current_angle);
    }

    ESP_LOGI(TAG, "Servo service initialised");
    return ESP_OK;
}

esp_err_t servo_service_start(void)
{
    if (s_task_running) {
        return ESP_OK;
    }
    s_task_running = true;

    BaseType_t ret = xTaskCreate(
        motion_task,
        "servo_task",
        SERVO_TASK_STACK_SIZE,
        NULL,
        SERVO_TASK_PRIORITY,
        &s_motion_task
    );
    if (ret != pdPASS) {
        s_task_running = false;
        ESP_LOGE(TAG, "Failed to create motion task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Servo service started");
    return ESP_OK;
}

esp_err_t servo_service_stop(void)
{
    if (!s_task_running) {
        return ESP_OK;
    }
    s_task_running = false;
    if (s_motion_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(SERVO_UPDATE_MS * 2)); /* let task finish cycle */
        s_motion_task = NULL;
    }

    /* Stop all servos */
    for (int i = 0; i < SERVO_MAX; i++) {
        if (s_servos[i].drv_handle) {
            servo_driver_stop(s_servos[i].drv_handle);
        }
    }
    ESP_LOGI(TAG, "Servo service stopped");
    return ESP_OK;
}

esp_err_t servo_service_register_handlers(void)
{
    esp_err_t ret;

    ret = service_register_command(CMD_SERVO_SET_ANGLE, handle_servo_set_angle, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_SERVO_STOP, handle_servo_stop, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_OPEN_LID, handle_servo_open, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_CLOSE_LID, handle_servo_close, NULL);
    if (ret != ESP_OK) return ret;

    /* Removed CMD_SERVO_OPEN / CMD_SERVO_CLOSE – domain commands not handled here */
    ESP_LOGI(TAG, "Servo command handlers registered (SET_ANGLE, STOP, OPEN_LID, CLOSE_LID)");
    return ESP_OK;
}