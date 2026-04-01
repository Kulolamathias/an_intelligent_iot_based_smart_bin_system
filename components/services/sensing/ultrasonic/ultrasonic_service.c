#include "ultrasonic_service.h"
#include "ultrasonic_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "mqtt_topic.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "ULTRASONIC_SVC";

/* Sensor pins */
#define FILL_SENSOR_TRIG 12
#define FILL_SENSOR_ECHO 13
#define INTENT_SENSOR_TRIG 2
#define INTENT_SENSOR_ECHO 4

/* Bin configuration */
#define BIN_HEIGHT_CM 60
#define FULL_THRESHOLD_CM 55
#define EMPTY_THRESHOLD_CM 2    /* distance below which bin is considered empty */
#define INTENT_DISTANCE_CM 25   /* distance below which intention is confirmed */

static ultrasonic_handle_t s_fill_sensor = NULL;
static ultrasonic_handle_t s_intent_sensor = NULL;
static TaskHandle_t s_intent_task = NULL;
static esp_timer_handle_t s_fill_timer = NULL;


/**
 * @brief Handle reading the fill level from the ultrasonic sensor
 * 
 * This function is called by the periodic timer and can also be triggered manually via a command.
 * It reads the distance from the fill sensor, converts it to a fill percentage, posts an event, and publishes to MQTT.
 * 
 * @param context Context for the command
 * @param params Parameters for the command
 * @return ESP_OK if successful, otherwise an error code
 */
static esp_err_t handle_read_fill_level(void *context, void *params);


static void fill_timer_callback(void *arg)
{
    (void)arg;
    handle_read_fill_level(NULL, NULL);
}

static uint8_t distance_to_fill_percent(uint32_t distance_cm)
{
    if (distance_cm >= FULL_THRESHOLD_CM) return 0;
    if (distance_cm <= EMPTY_THRESHOLD_CM) return 100;
    uint8_t percent = (uint8_t)( ( (FULL_THRESHOLD_CM - distance_cm) * 100) / FULL_THRESHOLD_CM);
    if (percent > 100) percent = 100;
    return percent;
}

static esp_err_t handle_read_fill_level(void *context, void *params)
{
    (void)context;
    (void)params;

    if (!s_fill_sensor) return ESP_ERR_INVALID_STATE;

    uint32_t pulse_us;
    esp_err_t ret = ultrasonic_driver_measure(s_fill_sensor, &pulse_us);
    fill_level_data_t data = { .fill_percent = 0 };

    if (ret == ESP_OK) {
        uint32_t distance_cm = (pulse_us * 1715) / 100000;
        // if (distance_cm > BIN_HEIGHT_CM) distance_cm = BIN_HEIGHT_CM; /**< cap to max height is handled in distance_to_fill_percent() */
        data.fill_percent = distance_to_fill_percent(distance_cm);
        ESP_LOGI(TAG, "Fill: distance=%lu cm, fill=%u%%", distance_cm, data.fill_percent);
    } else {
        ESP_LOGW(TAG, "Fill sensor failed: %d", ret);
        return ret;
    }

    system_event_t ev = {
        .id = EVENT_FILL_LEVEL_UPDATED,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    ev.data.fill_level.fill_percent = data.fill_percent;
    service_post_event(&ev);

    /* Publish fill level to MQTT */
    char topic[128];
    mqtt_topic_build(topic, sizeof(topic), "data");
    char json[64];
    snprintf(json, sizeof(json), "{\"fill\":%u}", data.fill_percent);
    cmd_publish_mqtt_params_t pub;
    strlcpy(pub.topic, topic, sizeof(pub.topic));
    strlcpy((char*)pub.payload, json, sizeof(pub.payload));
    pub.payload_len = strlen(json);
    pub.qos = 1;
    pub.retain = false;
    command_router_execute(CMD_PUBLISH_MQTT, &pub);
    ESP_LOGI(TAG, "Published fill level to %s\n\t\tFill Level: %u%% \n\tThe full json payload is: %s", pub.
        topic, data.fill_percent, pub.payload);

    return ESP_OK;
}

/* Intention sensor command handler (one-shot) */
static esp_err_t handle_read_intent_sensor(void *context, void *params)
{
    (void)context;
    (void)params;
    if (!s_intent_sensor) return ESP_ERR_INVALID_STATE;

    uint32_t pulse_us;
    esp_err_t ret = ultrasonic_driver_measure(s_intent_sensor, &pulse_us);
    if (ret == ESP_OK) {
        uint32_t distance_cm = (pulse_us * 1715) / 100000;
        ESP_LOGD(TAG, "Intent distance: %lu cm", distance_cm);
        if (distance_cm < INTENT_DISTANCE_CM) {
            system_event_t ev = {
                .id = EVENT_CLOSE_RANGE_DETECTED,
                .timestamp_us = esp_timer_get_time(),
                .source = 0,
                .data = { { {0} } }
            };
            service_post_event(&ev);
        }
    } else {
        ESP_LOGW(TAG, "Intent sensor failed: %d", ret);
    }
    return ESP_OK;
}

/* Periodic task to monitor intention sensor */
static void intent_monitor_task(void *arg)
{
    (void)arg;
    while (1) {
        handle_read_intent_sensor(NULL, NULL);
        vTaskDelay(pdMS_TO_TICKS(100)); /* check every 100 ms */
    }
}

/* Public API */
esp_err_t ultrasonic_service_init(void)
{
    ultrasonic_config_t fill_cfg = {
        .trig_pin = FILL_SENSOR_TRIG,
        .echo_pin = FILL_SENSOR_ECHO,
        .timeout_us = 100000
    };
    esp_err_t ret = ultrasonic_driver_create(&fill_cfg, &s_fill_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fill sensor init failed: %d", ret);
        return ret;
    }

    ultrasonic_config_t intent_cfg = {
        .trig_pin = INTENT_SENSOR_TRIG,
        .echo_pin = INTENT_SENSOR_ECHO,
        .timeout_us = 100000
    };
    ret = ultrasonic_driver_create(&intent_cfg, &s_intent_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Intent sensor init failed: %d", ret);
        ultrasonic_driver_delete(s_fill_sensor);
        return ret;
    }

    /* Creating periodic timer for fill level (1 seconds) using esp_timer */
    const esp_timer_create_args_t timer_args = {
        .callback = fill_timer_callback,
        .arg = NULL,
        .name = "fill_timer",
        .dispatch_method = ESP_TIMER_TASK,   // runs in the esp_timer task context
        .skip_unhandled_events = false
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_fill_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create fill timer: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Ultrasonic service initialised");
    return ESP_OK;
}

esp_err_t ultrasonic_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(COMMAND_READ_FILL_LEVEL, handle_read_fill_level, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_READ_INTENT_SENSOR, handle_read_intent_sensor, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Ultrasonic service command handlers registered");
    return ESP_OK;
}

esp_err_t ultrasonic_service_start(void)
{
    if (s_fill_timer) {
        esp_timer_start_periodic(s_fill_timer, 1 * 1000 * 1000); // 1 second in microseconds
    }

    /* Start periodic intention monitoring task */
    xTaskCreate(intent_monitor_task, "intent_monitor", 4096, NULL, 5, &s_intent_task);
    ESP_LOGI(TAG, "Ultrasonic service started (intention monitoring active)");
    return ESP_OK;
}

esp_err_t ultrasonic_service_stop(void)
{
    if (s_fill_timer) {
        esp_timer_stop(s_fill_timer);
        esp_timer_delete(s_fill_timer);
        s_fill_timer = NULL;
    }
    
    if (s_intent_task) {
        vTaskDelete(s_intent_task);
        s_intent_task = NULL;
    }
    ESP_LOGI(TAG, "Ultrasonic service stopped");
    return ESP_OK;
}







// #include "ultrasonic_service.h"
// #include "ultrasonic_driver.h"
// #include "service_interfaces.h"
// #include "command_params.h"
// #include "event_types.h"
// #include "esp_log.h"
// #include "esp_err.h"
// #include "esp_timer.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include <string.h>

// static const char *TAG = "ULTRASONIC_SVC";

// /* Sensor pins – adjust to your hardware */
// #define FILL_SENSOR_TRIG    12
// #define FILL_SENSOR_ECHO    13
// #define INTENT_SENSOR_TRIG  2
// #define INTENT_SENSOR_ECHO  4

// /* Bin configuration */
// #define BIN_HEIGHT_CM                           60  /* total height in cm */
// #define FULL_THRESHOLD_CM                       55  /* above this is considered 100% full */

// #define PERSON_PROXIMITY_INTENT_THRESHOLD_CM    25  /* if distance < this, consider it an intent signal */

// static ultrasonic_handle_t s_fill_sensor    = NULL;
// static ultrasonic_handle_t s_intent_sensor  = NULL;

// /* Intention monitoring task */
// static TaskHandle_t s_intent_task           = NULL;
// static volatile bool s_intent_task_running  = false;

// static uint8_t distance_to_fill_percent(uint32_t distance_cm)
// {
//     if (distance_cm >= FULL_THRESHOLD_CM) return 100;
//     if (distance_cm <= 0) return 0;
//     uint8_t percent = (uint8_t)((FULL_THRESHOLD_CM - distance_cm) * 100 / FULL_THRESHOLD_CM);
//     if (percent > 100) percent = 100;
//     return percent;
// }

// /* Task that monitors intention sensor every 500ms */
// static void intent_monitor_task(void *pvParameters)
// {
//     while (s_intent_task_running) {
//         uint32_t pulse_us;
//         esp_err_t ret = ultrasonic_driver_measure(s_intent_sensor, &pulse_us);
//         if (ret == ESP_OK) {
//             uint32_t distance_cm = (pulse_us * 1715) / 100000;
//             ESP_LOGD(TAG, "Intent distance: %lu cm", distance_cm);
//             /* Example: post event if distance < threshold */
//             if (distance_cm < PERSON_PROXIMITY_INTENT_THRESHOLD_CM) {
//                 system_event_t ev = {
//                     .id = EVENT_INTENT_SIGNAL_DETECTED,
//                     .timestamp_us = esp_timer_get_time(),
//                     .source = 0,
//                     .data = { { {0} } }
//                 };
//                 service_post_event(&ev);
//             }
//         } else {
//             ESP_LOGW(TAG, "Intent sensor failed: %d", ret);
//         }
//         vTaskDelay(pdMS_TO_TICKS(500));
//     }
//     vTaskDelete(NULL);
// }

// static esp_err_t handle_read_fill_level(void *context, void *params)
// {
//     (void)context;
//     (void)params;

//     if (!s_fill_sensor) return ESP_ERR_INVALID_STATE;

//     uint32_t pulse_us;
//     esp_err_t ret = ultrasonic_driver_measure(s_fill_sensor, &pulse_us);
//     fill_level_data_t data = { .fill_percent = 0 };
//     // fill_level_data_t data = { .validity.valid = (ret == ESP_OK), .fill_percent = 0 };

//     if (ret == ESP_OK) {
//         uint32_t distance_cm = (pulse_us * 1715) / 100000;
//         data.fill_percent = distance_to_fill_percent(distance_cm);
//         ESP_LOGI(TAG, "Fill: distance=%lu cm, fill=%u%%", distance_cm, data.fill_percent);
//     } else {
//         ESP_LOGW(TAG, "Fill sensor failed: %d", ret);
//     }

//     system_event_t ev = {
//         .id = EVENT_FILL_LEVEL_UPDATED,
//         .timestamp_us = esp_timer_get_time(),
//         .source = 0,
//         .data =  { { {0} } }
//     };
//     memcpy(&ev.data.fill_level, &data, sizeof(fill_level_data_t));
//     service_post_event(&ev);
//     return ESP_OK;
// }

// static esp_err_t handle_read_intent_sensor(void *context, void *params)
// {
//     (void)context;
//     (void)params;

//     if (!s_intent_sensor) return ESP_ERR_INVALID_STATE;

//     uint32_t pulse_us;
//     esp_err_t ret = ultrasonic_driver_measure(s_intent_sensor, &pulse_us);
//     if (ret == ESP_OK) {
//         uint32_t distance_cm = (pulse_us * 1715) / 100000;
//         ESP_LOGI(TAG, "Intent distance: %lu cm", distance_cm);
//     } else {
//         ESP_LOGW(TAG, "Intent sensor failed: %d", ret);
//     }
//     return ESP_OK;
// }

// esp_err_t ultrasonic_service_init(void)
// {
//     ultrasonic_config_t fill_cfg = {
//         .trig_pin = FILL_SENSOR_TRIG,
//         .echo_pin = FILL_SENSOR_ECHO,
//         .timeout_us = 100000
//     };
//     esp_err_t ret = ultrasonic_driver_create(&fill_cfg, &s_fill_sensor);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Fill sensor init failed: %d", ret);
//         return ret;
//     }

//     ultrasonic_config_t intent_cfg = {
//         .trig_pin = INTENT_SENSOR_TRIG,
//         .echo_pin = INTENT_SENSOR_ECHO,
//         .timeout_us = 100000
//     };
//     ret = ultrasonic_driver_create(&intent_cfg, &s_intent_sensor);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Intent sensor init failed: %d", ret);
//         ultrasonic_driver_delete(s_fill_sensor);
//         return ret;
//     }

//     ESP_LOGI(TAG, "Ultrasonic service initialised");
//     return ESP_OK;
// }

// esp_err_t ultrasonic_service_start(void)
// {
//     if (s_intent_task != NULL) {
//         ESP_LOGW(TAG, "Intention monitoring already running");
//         return ESP_OK;
//     }

//     s_intent_task_running = true;
//     BaseType_t ret = xTaskCreate(intent_monitor_task, "intent_monitor", 4096, NULL, 5, &s_intent_task);
//     if (ret != pdPASS) {
//         ESP_LOGE(TAG, "Failed to create intention monitor task");
//         s_intent_task_running = false;
//         return ESP_FAIL;
//     }
//     ESP_LOGI(TAG, "Ultrasonic service started (intention monitoring active)");
//     return ESP_OK;
// }

// esp_err_t ultrasonic_service_stop(void)
// {
//     if (s_intent_task == NULL) {
//         ESP_LOGW(TAG, "Intention monitoring not running");
//         return ESP_OK;
//     }

//     s_intent_task_running = false;
//     /* Wait for task to exit (it will delete itself) */
//     vTaskDelay(pdMS_TO_TICKS(100));
//     s_intent_task = NULL;
//     ESP_LOGI(TAG, "Ultrasonic service stopped");
//     return ESP_OK;
// }

// esp_err_t ultrasonic_service_register_handlers(void)
// {
//     esp_err_t ret;
//     ret = service_register_command(COMMAND_READ_FILL_LEVEL, handle_read_fill_level, NULL);
//     if (ret != ESP_OK) return ret;
//     ret = service_register_command(COMMAND_READ_INTENT_SENSOR, handle_read_intent_sensor, NULL);
//     if (ret != ESP_OK) return ret;
//     ESP_LOGI(TAG, "Ultrasonic service command handlers registered");
//     return ESP_OK;
// }