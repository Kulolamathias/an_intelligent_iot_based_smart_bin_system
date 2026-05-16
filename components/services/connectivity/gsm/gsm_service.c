/**
 * @file components/services/connectivity/gsm/gsm_service.c
 * @brief GSM Service – SMS sending and authentication event posting.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the GSM driver and provides a command interface for:
 *   - Sending SMS (CMD_SEND_SMS, CMD_SEND_SMS_RESPONSE)
 *   - Sending notifications (CMD_SEND_NOTIFICATION, CMD_ESCALATE_NOTIFICATION)
 *
 * It runs a background task that polls for incoming SMS, checks password and
 * authorised number, and posts EVENT_AUTH_GRANTED or EVENT_AUTH_DENIED to the core.
 *
 * It contains NO core state decisions – only observation (post events) and
 * command execution (send SMS).
 *
 * =============================================================================
 * @version 1.0.0
 * @author System Architecture Team
 * @date 2026
 */

#include "gsm_service.h"
#include "gsm_sim800.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "GSM_SVC";

/* -------------------------------------------------------------------------
 * Configuration (hardcoded – move to NVS later)
 * ------------------------------------------------------------------------- */
#define GSM_UART_PORT      UART_NUM_2
#define GSM_TX_PIN         GPIO_NUM_17
#define GSM_RX_PIN         GPIO_NUM_16
#define GSM_BAUD_RATE      9600
#define GSM_RX_BUFFER_SIZE 2048
#define GSM_CMD_TIMEOUT_MS 45000
#define GSM_RETRY_COUNT    2

#define SMS_PASSWORD       "SECRET123"
static const char *authorized_numbers[] = { "+255688173415" };
#define AUTHORIZED_COUNT   (sizeof(authorized_numbers) / sizeof(authorized_numbers[0]))

/* -------------------------------------------------------------------------
 * Service context (static)
 * ------------------------------------------------------------------------- */
static TaskHandle_t s_poll_task = NULL;
static bool s_running = false;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
static void poll_task(void *arg);
static void on_sms_received(const received_sms_t *sms);

/* -------------------------------------------------------------------------
 * Command handlers
 * ------------------------------------------------------------------------- */

static esp_err_t cmd_send_sms(void *context, void *params)
{
    (void)context;
    cmd_send_sms_params_t *p = (cmd_send_sms_params_t *)params;
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = gsm_send_sms(p->phone_number, p->message, GSM_CMD_TIMEOUT_MS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SMS sent to %s", p->phone_number);
    } else {
        ESP_LOGE(TAG, "Failed to send SMS to %s", p->phone_number);
    }
    return ret;
}

static esp_err_t cmd_send_sms_response(void *context, void *params)
{
    (void)context;
    cmd_send_sms_params_t *p = (cmd_send_sms_params_t *)params;
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = gsm_send_sms(p->phone_number, p->message, GSM_CMD_TIMEOUT_MS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Response SMS sent to %s", p->phone_number);
    } else {
        ESP_LOGE(TAG, "Failed to send response SMS to %s", p->phone_number);
    }
    return ret;
}

static esp_err_t cmd_send_notification(void *context, void *params)
{
    (void)context;
    cmd_send_notification_params_t *p = (cmd_send_notification_params_t *)params;
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Use the first authorised number as collector number */
    const char *collector = authorized_numbers[0];
    esp_err_t ret = gsm_send_sms(collector, p->message, GSM_CMD_TIMEOUT_MS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Notification sent to %s", collector);
    } else {
        ESP_LOGE(TAG, "Failed to send notification");
    }
    return ret;
}

static esp_err_t cmd_escalate_notification(void *context, void *params)
{
    (void)context;
    (void)params;
    /* Manager number – hardcoded; change as needed */
    const char *manager = "+255740073415";
    const char *msg = "URGENT: Bin full for >1 hour. Collector unresponsive.";
    esp_err_t ret = gsm_send_sms(manager, msg, GSM_CMD_TIMEOUT_MS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Escalation sent to manager %s", manager);
    } else {
        ESP_LOGE(TAG, "Failed to send escalation");
    }
    return ret;
}

/* -------------------------------------------------------------------------
 * Received SMS callback (called from gsm_process_received_sms)
 * ------------------------------------------------------------------------- */
static void on_sms_received(const received_sms_t *sms)
{
    if (!sms) {
        return;
    }

    ESP_LOGI(TAG, "Received SMS from %s: %s", sms->sender, sms->message);

    /* Check authorised number */
    bool authorized = false;
    for (size_t i = 0; i < AUTHORIZED_COUNT; i++) {
        if (strstr(sms->sender, authorized_numbers[i]) != NULL) {
            authorized = true;
            break;
        }
    }
    if (!authorized) {
        ESP_LOGW(TAG, "Unauthorized sender: %s", sms->sender);
        return;
    }

    /* Check password */
    if (strstr(sms->message, SMS_PASSWORD) == NULL) {
        ESP_LOGW(TAG, "Invalid password in SMS from %s", sms->sender);
        return;
    }

    /* Post authentication granted event to core */
    system_event_t ev = {
        .id = EVENT_AUTH_GRANTED,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { .gsm_command = { .sender = "", .command = "MAINTENANCE" } }
    };
    strlcpy(ev.data.gsm_command.sender, sms->sender, sizeof(ev.data.gsm_command.sender));
    service_post_event(&ev);

    ESP_LOGI(TAG, "EVENT_AUTH_GRANTED posted for %s", sms->sender);
}

/* -------------------------------------------------------------------------
 * Background task: polls GSM for new SMS and processes them
 * ------------------------------------------------------------------------- */
static void poll_task(void *arg)
{
    (void)arg;
    while (s_running) {
        esp_err_t ret = gsm_check_sms(true);          /* Read and delete SMS */
        if (ret == ESP_OK) {
            /* Process received SMS from queue (triggers callback) */
            int processed = gsm_process_received_sms();
            if (processed > 0) {
                ESP_LOGD(TAG, "Processed %d SMS messages", processed);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));              /* Poll every 5 seconds */
    }
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------
 * Public API (called by service_manager)
 * ------------------------------------------------------------------------- */

esp_err_t gsm_service_init(void)
{
    gsm_config_t config = {
        .uart_port   = GSM_UART_PORT,
        .tx_pin      = GSM_TX_PIN,
        .rx_pin      = GSM_RX_PIN,
        .baud_rate   = GSM_BAUD_RATE,
        .buf_size    = GSM_RX_BUFFER_SIZE,
        .timeout_ms  = GSM_CMD_TIMEOUT_MS,
        .retry_count = GSM_RETRY_COUNT,
    };

    esp_err_t ret = gsm_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gsm_init failed: %d", ret);
        return ret;
    }

    /* Set callbacks and authentication data */
    gsm_set_received_callback(on_sms_received);
    gsm_set_password(SMS_PASSWORD);
    gsm_set_authorized_numbers(authorized_numbers, AUTHORIZED_COUNT);

    ESP_LOGI(TAG, "GSM service initialised");
    return ESP_OK;
}

esp_err_t gsm_service_register_handlers(void)
{
    esp_err_t ret;

    ret = service_register_command(CMD_SEND_SMS, cmd_send_sms, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_SEND_SMS_RESPONSE, cmd_send_sms_response, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_SEND_NOTIFICATION, cmd_send_notification, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_ESCALATE_NOTIFICATION, cmd_escalate_notification, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "GSM command handlers registered");
    return ESP_OK;
}

esp_err_t gsm_service_start(void)
{
    if (s_poll_task != NULL) {
        ESP_LOGW(TAG, "GSM service already started");
        return ESP_OK;
    }

    s_running = true;
    BaseType_t ret = xTaskCreate(poll_task, "gsm_poll", 4096, NULL, 5, &s_poll_task);
    if (ret != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "Failed to create poll task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GSM service started (polling every 5s)");
    return ESP_OK;
}