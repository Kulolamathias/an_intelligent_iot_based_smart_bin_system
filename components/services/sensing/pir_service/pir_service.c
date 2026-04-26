/**
 * @file components/services/sensing/pir_service/pir_service.c
 * @brief PIR Motion Service – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses an ESP timer to poll the PIR sensor every 100 ms.
 * - Maintains previous motion state to detect rising/falling edges.
 * - Posts EVENT_PERSON_DETECTED on rising edge, EVENT_PERSON_LEFT on falling edge.
 * =============================================================================
 */

#include "pir_service.h"
#include "pir_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "PIR_SVC";

/* Configuration */
#define PIR_GPIO_PIN        GPIO_NUM_27   /* Adjust to your wiring */
#define PIR_POLL_INTERVAL_MS 100          /* 10 Hz polling rate */

static pir_handle_t s_pir = NULL;
static esp_timer_handle_t s_poll_timer = NULL;
static bool s_last_state = false;         /* true = motion detected */

/* Timer callback – polls sensor and posts events */
static void poll_timer_callback(void *arg)
{
    (void)arg;
    if (!s_pir) return;

    bool motion;
    esp_err_t ret = pir_driver_read(s_pir, &motion);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read PIR: %d", ret);
        return;
    }

    /* Detect edge changes */
    if (motion && !s_last_state) {
        /* Rising edge: person detected */
        system_event_t ev = {
            .id = EVENT_PERSON_DETECTED,
            .timestamp_us = esp_timer_get_time(),
            .source = 0,
            .data = { { {0} } }
        };
        service_post_event(&ev);
        ESP_LOGD(TAG, "EVENT_PERSON_DETECTED posted");
    } else if (!motion && s_last_state) {
        /* Falling edge: person left */
        system_event_t ev = {
            .id = EVENT_PERSON_LEFT,
            .timestamp_us = esp_timer_get_time(),
            .source = 0,
            .data = { { {0} } }
        };
        service_post_event(&ev);
        ESP_LOGD(TAG, "EVENT_PERSON_LEFT posted");
    }

    s_last_state = motion;
}

/* Command handlers */
static esp_err_t cmd_start_pir_monitoring(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!s_poll_timer) {
        ESP_LOGE(TAG, "Poll timer not created");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = esp_timer_start_periodic(s_poll_timer, PIR_POLL_INTERVAL_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start PIR polling: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "PIR monitoring started");
    return ESP_OK;
}

static esp_err_t cmd_stop_pir_monitoring(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (s_poll_timer) {
        esp_timer_stop(s_poll_timer);
        ESP_LOGI(TAG, "PIR monitoring stopped");
    }
    return ESP_OK;
}

/* Wrappers for command router (void* params) */
static esp_err_t cmd_start_pir_wrapper(void *context, void *params)
{
    return cmd_start_pir_monitoring(context, (const command_param_union_t*)params);
}
static esp_err_t cmd_stop_pir_wrapper(void *context, void *params)
{
    return cmd_stop_pir_monitoring(context, (const command_param_union_t*)params);
}

/* Public API */
esp_err_t pir_service_init(void)
{
    /* Create PIR driver */
    pir_config_t cfg = {
        .gpio_num = PIR_GPIO_PIN,
    };
    esp_err_t ret = pir_driver_create(&cfg, &s_pir);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PIR driver: %d", ret);
        return ret;
    }

    /* Create polling timer (initially stopped) */
    esp_timer_create_args_t timer_args = {
        .callback = poll_timer_callback,
        .arg = NULL,
        .name = "pir_poll"
    };
    ret = esp_timer_create(&timer_args, &s_poll_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PIR poll timer: %d", ret);
        pir_driver_delete(s_pir);
        return ret;
    }

    ESP_LOGI(TAG, "PIR service initialised (GPIO %d, polling %d ms)", PIR_GPIO_PIN, PIR_POLL_INTERVAL_MS);
    return ESP_OK;
}

esp_err_t pir_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_START_PIR_MONITORING, cmd_start_pir_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_STOP_PIR_MONITORING, cmd_stop_pir_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "PIR command handlers registered");
    return ESP_OK;
}

esp_err_t pir_service_start(void)
{
    /* Automatically start polling (can be overridden by command later) */
    esp_err_t ret = esp_timer_start_periodic(s_poll_timer, PIR_POLL_INTERVAL_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start PIR polling: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "PIR service started (polling active)");
    return ESP_OK;
}

esp_err_t pir_service_stop(void)
{
    if (s_poll_timer) {
        esp_timer_stop(s_poll_timer);
    }
    if (s_pir) {
        pir_driver_delete(s_pir);
        s_pir = NULL;
    }
    if (s_poll_timer) {
        esp_timer_delete(s_poll_timer);
        s_poll_timer = NULL;
    }
    ESP_LOGI(TAG, "PIR service stopped");
    return ESP_OK;
}