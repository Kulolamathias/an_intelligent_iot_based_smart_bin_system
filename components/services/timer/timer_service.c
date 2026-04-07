/**
 * @file components/services/timer/timer_service.c
 * @brief Timer Service – using ESP timers (esp_timer).
 *
 * =============================================================================
 * Uses ESP timers (esp_timer) instead of FreeRTOS software timers.
 * ESP timers run in a dedicated high‑priority task and are more reliable.
 * =============================================================================
 */

#include "timer_service.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "TIMER_SVC";

/* ============================================================
 * Timer handles (esp_timer)
 * ============================================================ */
static esp_timer_handle_t s_intent_timer = NULL;
static esp_timer_handle_t s_escalation_timer = NULL;
static esp_timer_handle_t s_periodic_timer = NULL;
static esp_timer_handle_t s_oneshot_timer = NULL;

/* ============================================================
 * Timer callback functions
 * ============================================================ */

static void intent_timer_callback(void *arg)
{
    (void)arg;
    system_event_t ev = {
        .id = EVENT_INTENT_TIMEOUT,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "EVENT_INTENT_TIMEOUT posted");
}

static void escalation_timer_callback(void *arg)
{
    (void)arg;
    system_event_t ev = {
        .id = EVENT_ESCALATION_TIMEOUT,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "EVENT_ESCALATION_TIMEOUT posted");
}

static void periodic_timer_callback(void *arg)
{
    (void)arg;
    system_event_t ev = {
        .id = EVENT_PERIODIC_REPORT_TIMEOUT,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "EVENT_PERIODIC_REPORT_TIMEOUT posted");
}

static void oneshot_timer_callback(void *arg)
{
    (void)arg;
    system_event_t ev = {
        .id = EVENT_ONESHOT_REPORT_TIMEOUT,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "EVENT_ONESHOT_REPORT_TIMEOUT posted");
}

/* ============================================================
 * Command handlers
 * ============================================================ */

static esp_err_t handle_start_intent_timer(void *context, void *params)
{
    (void)context;
    if (params == NULL) {
        ESP_LOGE(TAG, "CMD_START_INTENT_TIMER called without parameters");
        return ESP_ERR_INVALID_ARG;
    }
    cmd_start_timer_params_t *p = (cmd_start_timer_params_t*)params;

    /* Stop timer if running */
    if (s_intent_timer) {
        esp_timer_stop(s_intent_timer);
    }

    /* Start timer (one‑shot) */
    esp_err_t ret = esp_timer_start_once(s_intent_timer, p->timeout_ms * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start intent timer: %d", ret);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Intent timer started with %lu ms", p->timeout_ms);
    return ESP_OK;
}

static esp_err_t handle_stop_intent_timer(void *context, void *params)
{
    (void)context;
    (void)params;
    if (s_intent_timer) {
        esp_timer_stop(s_intent_timer);
        ESP_LOGD(TAG, "Intent timer stopped");
    }
    return ESP_OK;
}

static esp_err_t handle_start_escalation_timer(void *context, void *params)
{
    (void)context;
    if (params == NULL) {
        ESP_LOGE(TAG, "CMD_START_ESCALATION_TIMER called without parameters");
        return ESP_ERR_INVALID_ARG;
    }
    cmd_start_timer_params_t *p = (cmd_start_timer_params_t*)params;

    if (s_escalation_timer) {
        esp_timer_stop(s_escalation_timer);
        esp_err_t ret = esp_timer_start_once(s_escalation_timer, p->timeout_ms * 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start escalation timer: %d", ret);
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Escalation timer started with %lu ms", p->timeout_ms);
    }
    return ESP_OK;
}

static esp_err_t handle_stop_escalation_timer(void *context, void *params)
{
    (void)context;
    (void)params;
    if (s_escalation_timer) {
        esp_timer_stop(s_escalation_timer);
        ESP_LOGD(TAG, "Escalation timer stopped");
    }
    return ESP_OK;
}

static esp_err_t handle_start_periodic_timer(void *context, void *params)
{
    (void)context;
    if (params == NULL) {
        ESP_LOGE(TAG, "CMD_START_PERIODIC_TIMER called without parameters");
        return ESP_ERR_INVALID_ARG;
    }
    cmd_start_timer_params_t *p = (cmd_start_timer_params_t*)params;

    if (s_periodic_timer) {
        esp_timer_stop(s_periodic_timer);
        esp_err_t ret = esp_timer_start_periodic(s_periodic_timer, p->timeout_ms * 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start periodic timer: %d", ret);
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Periodic timer started with %lu ms", p->timeout_ms);
    }
    return ESP_OK;
}

static esp_err_t handle_start_oneshot_timer(void *context, void *params)
{
    (void)context;
    if (params == NULL) {
        ESP_LOGE(TAG, "CMD_START_ONE_SHOT_TIMER called without parameters");
        return ESP_ERR_INVALID_ARG;
    }
    cmd_start_timer_params_t *p = (cmd_start_timer_params_t*)params;

    if (s_oneshot_timer) {
        esp_timer_stop(s_oneshot_timer);
        esp_err_t ret = esp_timer_start_once(s_oneshot_timer, p->timeout_ms * 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start one‑shot timer: %d", ret);
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "One‑shot timer started with %lu ms", p->timeout_ms);
    }
    return ESP_OK;
}

static esp_err_t handle_stop_periodic_timer(void *context, void *params)
{
    (void)context;
    (void)params;
    if (s_periodic_timer) {
        esp_timer_stop(s_periodic_timer);
        ESP_LOGD(TAG, "Periodic timer stopped");
    }
    return ESP_OK;
}

static esp_err_t handle_stop_oneshot_timer(void *context, void *params)
{
    (void)context;
    (void)params;
    if (s_oneshot_timer) {
        esp_timer_stop(s_oneshot_timer);
        ESP_LOGD(TAG, "One‑shot timer stopped");
    }
    return ESP_OK;
}

/* ============================================================
 * Base contract implementation
 * ============================================================ */

esp_err_t timer_service_init(void)
{
    /* Create ESP timers */
    esp_timer_create_args_t intent_args = {
        .callback = intent_timer_callback,
        .arg = NULL,
        .name = "intent_timer"
    };
    esp_err_t ret = esp_timer_create(&intent_args, &s_intent_timer);
    if (ret != ESP_OK) goto fail;

    esp_timer_create_args_t escalation_args = {
        .callback = escalation_timer_callback,
        .arg = NULL,
        .name = "escalation_timer"
    };
    ret = esp_timer_create(&escalation_args, &s_escalation_timer);
    if (ret != ESP_OK) goto fail;

    esp_timer_create_args_t periodic_args = {
        .callback = periodic_timer_callback,
        .arg = NULL,
        .name = "periodic_timer"
    };
    ret = esp_timer_create(&periodic_args, &s_periodic_timer);
    if (ret != ESP_OK) goto fail;

    esp_timer_create_args_t oneshot_args = {
        .callback = oneshot_timer_callback,
        .arg = NULL,
        .name = "oneshot_timer"
    };
    ret = esp_timer_create(&oneshot_args, &s_oneshot_timer);
    if (ret != ESP_OK) goto fail;

    ESP_LOGI(TAG, "Timer service initialised (using esp_timer)");
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Failed to create timer");
    if (s_intent_timer) esp_timer_delete(s_intent_timer);
    if (s_escalation_timer) esp_timer_delete(s_escalation_timer);
    if (s_periodic_timer) esp_timer_delete(s_periodic_timer);
    if (s_oneshot_timer) esp_timer_delete(s_oneshot_timer);
    return ESP_FAIL;
}

esp_err_t timer_service_start(void)
{
    ESP_LOGI(TAG, "Timer service started");
    return ESP_OK;
}

esp_err_t timer_service_stop(void)
{
    if (s_intent_timer) esp_timer_stop(s_intent_timer);
    if (s_escalation_timer) esp_timer_stop(s_escalation_timer);
    if (s_periodic_timer) esp_timer_stop(s_periodic_timer);
    if (s_oneshot_timer) esp_timer_stop(s_oneshot_timer);
    ESP_LOGI(TAG, "Timer service stopped");
    return ESP_OK;
}

esp_err_t timer_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_START_INTENT_TIMER, handle_start_intent_timer, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_STOP_INTENT_TIMER, handle_stop_intent_timer, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_START_ESCALATION_TIMER, handle_start_escalation_timer, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_STOP_ESCALATION_TIMER, handle_stop_escalation_timer, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_START_PERIODIC_TIMER, handle_start_periodic_timer, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_STOP_PERIODIC_TIMER, handle_stop_periodic_timer, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_START_ONESHOT_TIMER, handle_start_oneshot_timer, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_STOP_ONESHOT_TIMER, handle_stop_oneshot_timer, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Timer command handlers registered");
    return ESP_OK;
}