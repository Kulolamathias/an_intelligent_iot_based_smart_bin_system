/**
 * @file timer_service.c
 * @brief Timer Service implementation.
 *
 * =============================================================================
 * Uses FreeRTOS software timers. All timer handles are static.
 * Command handlers receive duration via cmd_start_timer_params_t.
 * Timeout events are posted via service_post_event().
 * =============================================================================
 */

#include "timer_service.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "TIMER_SVC";

/* ============================================================
 * Static timer handles
 * ============================================================ */
static TimerHandle_t s_intent_timer = NULL;
static TimerHandle_t s_escalation_timer = NULL;
static TimerHandle_t s_periodic_timer = NULL;
static TimerHandle_t s_oneshot_timer = NULL;

/* ============================================================
 * Timer callback functions
 * ============================================================ */

static void intent_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    system_event_t ev = {
        .id = EVENT_INTENT_TIMEOUT,
        .data = { {0} }   // zero payload /**<TODO: to be reviewed (this line) */
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "EVENT_INTENT_TIMEOUT posted");
}

static void escalation_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    system_event_t ev = {
        .id = EVENT_ESCALATION_TIMEOUT,
        .data = { {0} }   // zero payload /**<TODO: to be reviewed (this line) */
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "EVENT_ESCALATION_TIMEOUT posted");
}

static void periodic_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    system_event_t ev = {
        .id = EVENT_PERIODIC_REPORT_TIMEOUT,
        .data = { {0} }   // zero payload /**<TODO: to be reviewed (this line) */
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "EVENT_PERIODIC_REPORT_TIMEOUT posted");
}

static void oneshot_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    system_event_t ev = {
        .id = EVENT_ONESHOT_REPORT_TIMEOUT,
        .data = { {0} }   // zero payload /**<TODO: to be reviewed (this line) */
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

    /* Stop timer if running (safe to call even if not running) */
    xTimerStop(s_intent_timer, 0);

    /* Change period and start */
    if (xTimerChangePeriod(s_intent_timer, pdMS_TO_TICKS(p->timeout_ms), 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start intent timer");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Intent timer started with %lu ms", p->timeout_ms);
    return ESP_OK;
}

static esp_err_t handle_stop_intent_timer(void *context, void *params)
{
    (void)context;
    (void)params;
    if (xTimerStop(s_intent_timer, 0) != pdPASS) {
        /* Timer may not be running – that's fine */
        ESP_LOGD(TAG, "Intent timer stop (maybe not running)");
    } else {
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

    xTimerStop(s_escalation_timer, 0);
    if (xTimerChangePeriod(s_escalation_timer, pdMS_TO_TICKS(p->timeout_ms), 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start escalation timer");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Escalation timer started with %lu ms", p->timeout_ms);
    return ESP_OK;
}

static esp_err_t handle_stop_escalation_timer(void *context, void *params)
{
    (void)context;
    (void)params;
    xTimerStop(s_escalation_timer, 0);
    ESP_LOGD(TAG, "Escalation timer stopped");
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

    xTimerStop(s_periodic_timer, 0);
    if (xTimerChangePeriod(s_periodic_timer, pdMS_TO_TICKS(p->timeout_ms), 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start periodic timer");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Periodic timer started with %lu ms", p->timeout_ms);
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

    xTimerStop(s_oneshot_timer, 0);
    if (xTimerChangePeriod(s_oneshot_timer, pdMS_TO_TICKS(p->timeout_ms), 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start one-shot timer");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "One-shot timer started with %lu ms", p->timeout_ms);
    return ESP_OK;

    /**  
     * In a real implementation, you might want to create and start a new one‑shot timer here,
     * or reuse a static one‑shot timer with parameterized period. For simplicity, we reuse s_oneshot_timer.
     * 
     * Create a one‑shot timer on the fly (without tracking the handle) – this is just for demonstration and not ideal for production code.
    TimerHandle_t oneshot_timer = xTimerCreate(
        "oneshot_tmr",
        pdMS_TO_TICKS(p->timeout_ms),
        pdFALSE,
        NULL,
        oneshot_timer_callback  // Reuse existing callback for simplicity – in a real implementation, you might want a separate callback or context to distinguish timers
    );

    if (oneshot_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create one‑shot timer");
        return ESP_FAIL;
    }

    if (xTimerStart(oneshot_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start one‑shot timer");
        xTimerDelete(oneshot_timer, 0);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "One‑shot timer started with %lu ms", p->timeout_ms);
    return ESP_OK;

    */
}

static esp_err_t handle_stop_periodic_timer(void *context, void *params)
{
    (void)context;
    (void)params;
    xTimerStop(s_periodic_timer, 0);
    ESP_LOGD(TAG, "Periodic timer stopped");
    return ESP_OK;
}

static esp_err_t handle_stop_oneshot_timer(void *context, void *params)
{
    (void)context;
    (void)params;
    /* In a real implementation, you would need to track the handle of the one‑shot timer to stop it. */
    ESP_LOGW(TAG, "Stopping one‑shot timer not implemented (no handle tracking)");
    return ESP_OK;
}

/* ============================================================
 * Base contract implementation
 * ============================================================ */

esp_err_t timer_service_init(void)
{
    /* Create timers (all initially stopped) */
    s_intent_timer = xTimerCreate(
        "intent_tmr",
        pdMS_TO_TICKS(1000),  /* dummy period, will be changed on start */
        pdFALSE,               /* one‑shot */
        NULL,
        intent_timer_callback
    );

    s_escalation_timer = xTimerCreate(
        "escalation_tmr",
        pdMS_TO_TICKS(1000),
        pdFALSE,
        NULL,
        escalation_timer_callback
    );

    s_periodic_timer = xTimerCreate(
        "periodic_tmr",
        pdMS_TO_TICKS(1000),
        pdTRUE,                /* auto‑reload for periodic timer */
        NULL,
        periodic_timer_callback
    );

    s_oneshot_timer = xTimerCreate(
        "oneshot_tmr",
        pdMS_TO_TICKS(1000),
        pdFALSE,
        NULL,
        oneshot_timer_callback
    );

    if (s_intent_timer == NULL || s_escalation_timer == NULL || s_periodic_timer == NULL || s_oneshot_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timers");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Timer service initialised");
    return ESP_OK;
}

esp_err_t timer_service_start(void)
{
    /* Timers are started on demand; nothing to do here */
    ESP_LOGI(TAG, "Timer service started");
    return ESP_OK;
}

esp_err_t timer_service_stop(void)
{
    /* Stop all timers */
    xTimerStop(s_intent_timer,      0);
    xTimerStop(s_escalation_timer,  0);
    xTimerStop(s_periodic_timer,    0);
    xTimerStop(s_oneshot_timer,     0);
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