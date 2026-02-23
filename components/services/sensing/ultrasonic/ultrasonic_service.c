/**
 * @file ultrasonic_service.c
 * @brief Ultrasonic service implementation.
 *
 * =============================================================================
 * Manages two sensor instances:
 *   - fill_sensor (for fill level)
 *   - intent_sensor (for close-range detection)
 *
 * A FreeRTOS timer periodically polls the intent sensor and posts
 * EVENT_CLOSE_RANGE_DETECTED / EVENT_CLOSE_RANGE_LOST when state changes.
 *
 * The fill sensor is triggered on command (CMD_READ_FILL_LEVEL) and posts
 * EVENT_BIN_LEVEL_UPDATED with the computed percentage.
 * =============================================================================
 */

#include "ultrasonic_service.h"
#include "service_interfaces.h"
#include "ultrasonic_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* ============================================================
 * Kconfig / Default Configuration (to be replaced by config service)
 * ============================================================ */
#define FILL_SENSOR_TRIG_PIN        6       /**<  */
#define FILL_SENSOR_ECHO_PIN        7
#define INTENT_SENSOR_TRIG_PIN      4
#define INTENT_SENSOR_ECHO_PIN      5
#define SENSOR_TIMEOUT_US           30000   /* 30ms max range ~5m */
#define INTENT_THRESHOLD_CM         30      /* distance < 30cm = person present */
#define BIN_HEIGHT_CM               100     /* total bin height (example) */
#define INTENT_POLL_PERIOD_MS       100     /* poll intent sensor every 100ms */

static const char *TAG = "ULTRASONIC_SVC";

/* ============================================================
 * Static handles and state
 * ============================================================ */
static ultrasonic_handle_t s_fill_sensor = NULL;
static ultrasonic_handle_t s_intent_sensor = NULL;
static TimerHandle_t s_intent_timer = NULL;
static bool s_intent_last_detected = false;   /* debounce / state change detection */

/* ============================================================
 * Helper: convert pulse width to distance in cm
 * ============================================================ */
static uint32_t pulse_to_cm(uint32_t pulse_us)
{
    /* Speed of sound ~340 m/s => 29.15 µs per cm (round trip) */
    return pulse_us / 58;   /* typical formula: pulse_us/58 = cm */
}

/* ============================================================
 * Helper: measure intent sensor and post events if changed
 * ============================================================ */
static void check_intent_sensor(void)
{
    uint32_t pulse = 0;
    esp_err_t ret = ultrasonic_driver_measure(s_intent_sensor, &pulse);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Intent sensor measure failed: %d", ret);
        return;
    }

    uint32_t distance_cm = pulse_to_cm(pulse);
    bool detected = (distance_cm < INTENT_THRESHOLD_CM);

    if (detected != s_intent_last_detected) {
        system_event_t ev = {
            .id = detected ? EVENT_CLOSE_RANGE_DETECTED : EVENT_CLOSE_RANGE_LOST,
            .data = { {0} }
        };
        service_post_event(&ev);
        ESP_LOGD(TAG, "Intent state changed: %s", detected ? "DETECTED" : "LOST");
        s_intent_last_detected = detected;
    }
}

/* ============================================================
 * Timer callback for intent polling
 * ============================================================ */
static void intent_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    check_intent_sensor();
}

/* ============================================================
 * Command handler for CMD_READ_FILL_LEVEL
 * ============================================================ */
static esp_err_t handle_read_fill_level(void *context, void *params)
{
    (void)context;
    (void)params;   /* no parameters expected */

    if (s_fill_sensor == NULL) {
        ESP_LOGE(TAG, "Fill sensor not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t pulse = 0;
    esp_err_t ret = ultrasonic_driver_measure(s_fill_sensor, &pulse);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fill sensor measurement failed: %d", ret);
        return ret;
    }

    uint32_t distance_cm = pulse_to_cm(pulse);
    /* Compute fill percentage, clamped to 0-100 */
    uint32_t fill_percent = (distance_cm >= BIN_HEIGHT_CM) ? 0 :
                            (100 - (distance_cm * 100 / BIN_HEIGHT_CM));
    if (fill_percent > 100) fill_percent = 100;

    system_event_t ev = {
        .id = EVENT_BIN_LEVEL_UPDATED,
        .data.bin_level.fill_level_percent = (uint8_t)fill_percent
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "Fill level: %"PRIu32" (distance %"PRIu32" cm)", fill_percent, distance_cm);
    return ESP_OK;
}

/* ============================================================
 * Command handler for CMD_READ_INTENT_SENSOR (immediate read)
 * ============================================================ */
static esp_err_t handle_read_intent_sensor(void *context, void *params)
{
    (void)context;
    (void)params;   /* no parameters expected */

    if (s_intent_sensor == NULL) {
        ESP_LOGE(TAG, "Intent sensor not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t pulse = 0;
    esp_err_t ret = ultrasonic_driver_measure(s_intent_sensor, &pulse);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Intent sensor measurement failed: %d", ret);
        return ret;
    }

    uint32_t distance_cm = pulse_to_cm(pulse);
    system_event_t ev = {
        .id = EVENT_INTENT_SENSOR_READ,
        .data.intent_sensor.distance_cm = (uint16_t)distance_cm
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "Intent sensor read: %"PRIu32" cm", distance_cm);
    return ESP_OK;
}

/* ============================================================
 * Base contract implementation
 * ============================================================ */

esp_err_t ultrasonic_service_init(void)
{
    ultrasonic_config_t cfg;

    /* Create fill sensor */
    cfg.trig_pin = FILL_SENSOR_TRIG_PIN;
    cfg.echo_pin = FILL_SENSOR_ECHO_PIN;
    cfg.timeout_us = SENSOR_TIMEOUT_US;
    esp_err_t ret = ultrasonic_driver_create(&cfg, &s_fill_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create fill sensor: %d", ret);
        return ret;
    }

    /* Create intent sensor */
    cfg.trig_pin = INTENT_SENSOR_TRIG_PIN;
    cfg.echo_pin = INTENT_SENSOR_ECHO_PIN;
    ret = ultrasonic_driver_create(&cfg, &s_intent_sensor);
    if (ret != ESP_OK) {
        ultrasonic_driver_delete(s_fill_sensor);
        s_fill_sensor = NULL;
        ESP_LOGE(TAG, "Failed to create intent sensor: %d", ret);
        return ret;
    }

    /* Create timer for intent polling (not started yet) */
    s_intent_timer = xTimerCreate("intent_poll",
                                  pdMS_TO_TICKS(INTENT_POLL_PERIOD_MS),
                                  pdTRUE,        /* auto-reload */
                                  NULL,
                                  intent_timer_callback);
    if (s_intent_timer == NULL) {
        ultrasonic_driver_delete(s_fill_sensor);
        ultrasonic_driver_delete(s_intent_sensor);
        s_fill_sensor = s_intent_sensor = NULL;
        ESP_LOGE(TAG, "Failed to create intent timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Ultrasonic service initialised");
    return ESP_OK;
}

esp_err_t ultrasonic_service_start(void)
{
    if (s_intent_timer != NULL) {
        xTimerStart(s_intent_timer, 0);
        ESP_LOGI(TAG, "Intent polling started");
    }
    return ESP_OK;
}

esp_err_t ultrasonic_service_stop(void)
{
    if (s_intent_timer != NULL) {
        xTimerStop(s_intent_timer, 0);
        ESP_LOGI(TAG, "Intent polling stopped");
    }
    return ESP_OK;
}

esp_err_t ultrasonic_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_READ_FILL_LEVEL, handle_read_fill_level, NULL);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to register CMD_READ_FILL_LEVEL handler"); return ret; }

    ret = service_register_command(CMD_READ_INTENT_SENSOR, handle_read_intent_sensor, NULL);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to register CMD_READ_INTENT_SENSOR handler"); return ret; }
    
    ESP_LOGI(TAG, "Ultrasonic command handlers registered");
    return ESP_OK;
}