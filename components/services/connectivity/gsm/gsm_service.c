/**
 * @file gsm_service.c
 * @brief GSM Service implementation.
 *
 * =============================================================================
 * This service:
 *   - Maintains password and authorized number list.
 *   - Runs a task to periodically check for incoming SMS.
 *   - Runs a task to process async SMS send queue.
 *   - Authenticates incoming SMS and emits events.
 *   - Receives commands (e.g., CMD_SEND_SMS) via command_router.
 * =============================================================================
 */

#include "gsm_service.h"
#include "service_interfaces.h"
#include "command_params.h"          // for cmd_send_sms_params_t (needs definition)
#include "event_types.h"              // core events
#include "command_types.h"            // core commands
#include "gsm_sim800_driver.h"        // low-level driver

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "GSM_SVC";

/* ============================================================
 * Internal types and state
 * ============================================================ */

typedef struct {
    char number[16];
    char message[161];
} pending_sms_t;

typedef struct {
    char number[16];
    char message[161];
    char timestamp[32];
} received_sms_t;

typedef struct {
    char password[33];
    const char **auth_numbers;
    uint8_t auth_count;
    QueueHandle_t send_queue;          // pending_sms_t
    QueueHandle_t recv_queue;          // received_sms_t (for internal use, or we directly emit events)
    TaskHandle_t check_task;            // periodic SMS check
    TaskHandle_t send_task;             // async sender
    SemaphoreHandle_t mutex;
    bool running;
    TaskHandle_t keepalive_task;
} gsm_service_ctx_t;

static gsm_service_ctx_t s_ctx = {0};

/* ============================================================
 * Forward declarations
 * ============================================================ */
static void check_sms_task(void *arg);
static void send_sms_task(void *arg);
static void process_sms(const gsm_raw_sms_t *raw);
static bool is_authorized(const char *number);
static bool contains_password(const char *message);

static void keepalive_task(void *arg);

/* ============================================================
 * Command handlers
 * ============================================================ */

static esp_err_t handle_send_sms(void *context, void *params)
{
    (void)context;
    if (params == NULL) return ESP_ERR_INVALID_ARG;
    cmd_send_sms_params_t *p = (cmd_send_sms_params_t*)params;

    pending_sms_t sms;
    strncpy(sms.number, p->phone_number, sizeof(sms.number)-1);
    strncpy(sms.message, p->message, sizeof(sms.message)-1);
    if (xQueueSend(s_ctx.send_queue, &sms, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Send queue full");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Additional command handlers (set password, set auth numbers) could be added,
// but they are typically called directly from main via config functions.

/* ============================================================
 * Service contract
 * ============================================================ */

esp_err_t gsm_service_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_ctx.mutex) return ESP_ERR_NO_MEM;

    s_ctx.send_queue = xQueueCreate(5, sizeof(pending_sms_t));
    s_ctx.recv_queue = xQueueCreate(10, sizeof(received_sms_t));
    if (!s_ctx.send_queue || !s_ctx.recv_queue) {
        if (s_ctx.send_queue) vQueueDelete(s_ctx.send_queue);
        if (s_ctx.recv_queue) vQueueDelete(s_ctx.recv_queue);
        vSemaphoreDelete(s_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    // Initialize driver (default config)
    esp_err_t ret = gsm_driver_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Driver init failed");
        return ret;
    }

    ESP_LOGI(TAG, "Service initialised");
    return ESP_OK;
}

esp_err_t gsm_service_start(void)
{
    s_ctx.running = true;
    xTaskCreate(check_sms_task, "gsm_check", 8192, NULL, 3, &s_ctx.check_task);
    xTaskCreate(send_sms_task, "gsm_send", 6144, NULL, 3, &s_ctx.send_task);
    xTaskCreate(keepalive_task, "gsm_keep", 4096, NULL, 2, &s_ctx.keepalive_task);
    ESP_LOGI(TAG, "Service started");
    return ESP_OK;
}

esp_err_t gsm_service_stop(void)
{
    s_ctx.running = false;
    if (s_ctx.check_task) {
        vTaskDelete(s_ctx.check_task);
        s_ctx.check_task = NULL;
    }
    if (s_ctx.send_task) {
        vTaskDelete(s_ctx.send_task);
        s_ctx.send_task = NULL;
    }
    if (s_ctx.keepalive_task) {
        vTaskDelete(s_ctx.keepalive_task);
        s_ctx.keepalive_task = NULL;
    }
    ESP_LOGI(TAG, "Service stopped");
    return ESP_OK;
}

esp_err_t gsm_service_register_handlers(void)
{
    // Register commands: CMD_SEND_SMS (assume defined)
    esp_err_t ret = service_register_command(CMD_SEND_SMS, handle_send_sms, NULL);
    if (ret != ESP_OK) return ret;

    // Optional: CMD_SET_GSM_PASSWORD, CMD_SET_AUTH_NUMBERS could be added
    ESP_LOGI(TAG, "Handlers registered");
    return ESP_OK;
}

/* ============================================================
 * Configuration setters
 * ============================================================ */

void gsm_service_set_password(const char *password)
{
    if (xSemaphoreTake(s_ctx.mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(s_ctx.password, password, sizeof(s_ctx.password)-1);
        xSemaphoreGive(s_ctx.mutex);
        ESP_LOGI(TAG, "Password set");
    }
}

void gsm_service_set_authorized_numbers(const char **numbers, uint8_t count)
{
    if (xSemaphoreTake(s_ctx.mutex, portMAX_DELAY) == pdTRUE) {
        s_ctx.auth_numbers = numbers;
        s_ctx.auth_count = count;
        xSemaphoreGive(s_ctx.mutex);
        ESP_LOGI(TAG, "Authorized numbers set (%d)", count);
    }
}

/* ============================================================
 * Internal helper functions
 * ============================================================ */

static bool is_authorized(const char *number)
{
    if (s_ctx.auth_count == 0) return true;  // all allowed
    if (!s_ctx.auth_numbers) return false;
    char cleaned[16];
    strncpy(cleaned, number, sizeof(cleaned)-1);
    // Clean number (keep digits and +)
    char *src = cleaned, *dst = cleaned;
    while (*src) {
        if (isdigit((unsigned char)*src) || *src == '+') *dst++ = *src;
        src++;
    }
    *dst = '\0';

    for (uint8_t i = 0; i < s_ctx.auth_count; i++) {
        const char *auth = s_ctx.auth_numbers[i];
        if (!auth) continue;
        char auth_clean[16];
        strncpy(auth_clean, auth, sizeof(auth_clean)-1);
        src = auth_clean; dst = auth_clean;
        while (*src) {
            if (isdigit((unsigned char)*src) || *src == '+') *dst++ = *src;
            src++;
        }
        *dst = '\0';
        if (strcmp(cleaned, auth_clean) == 0) return true;
    }
    return false;
}

static bool contains_password(const char *message)
{
    return (strstr(message, s_ctx.password) != NULL);
}

static void process_sms(const gsm_raw_sms_t *raw)
{
    ESP_LOGI(TAG, "Received SMS from %s: %s", raw->sender, raw->message);

    // 1. Check authorization
    if (!is_authorized(raw->sender)) {
        ESP_LOGW(TAG, "Unauthorized sender: %s", raw->sender);
        // Optionally send failure notification? Not service's role.
        // Emit EVENT_AUTH_DENIED
        system_event_t ev = { .id = EVENT_AUTH_DENIED, .data = { {0} } };
        service_post_event(&ev);
        return;
    }

    // 2. Check password presence
    if (!contains_password(raw->message)) {
        ESP_LOGW(TAG, "Invalid password in message");
        system_event_t ev = { .id = EVENT_AUTH_DENIED, .data = { {0} } };
        service_post_event(&ev);
        return;
    }

    // 3. Authorized and password OK – emit granted event
    system_event_t auth_ev = { .id = EVENT_AUTH_GRANTED, .data = { {0} } };
    service_post_event(&auth_ev);

    // 4. Extract command part (everything after password? We'll just forward full message)
    // Core will parse further. We emit a generic SMS_RECEIVED event with payload.
    // We need to define EVENT_GSM_SMS_RECEIVED in core.
    // Payload: sender + message.
    // For simplicity, we can reuse EVENT_NETWORK_MESSAGE_RECEIVED? But that's generic.
    // We'll define a new event: EVENT_GSM_COMMAND_RECEIVED.
    // Let's assume it exists.
    #if EVENT_GSM_COMMAND_RECEIVED  // ut was originally #ifdef not #if
    system_event_t cmd_ev = {
        .id = EVENT_GSM_COMMAND_RECEIVED,
        .data = {
            .gsm_command = {
                .sender = {0},
                .command = {0}
            }
        }
    };
    strncpy(cmd_ev.data.gsm_command.sender, raw->sender, sizeof(cmd_ev.data.gsm_command.sender)-1);
    strncpy(cmd_ev.data.gsm_command.command, raw->message, sizeof(cmd_ev.data.gsm_command.command)-1);
    service_post_event(&cmd_ev);
    #endif
}

/* ============================================================
 * Tasks
 * ============================================================ */

static void check_sms_task(void *arg)
{
    char list_buf[2048];
    uint8_t indices[32];
    int count;

    while (s_ctx.running) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // check every 5 seconds

        // List all SMS
        if (gsm_driver_list_sms(list_buf, sizeof(list_buf)) != ESP_OK) {
            continue;
        }
        parse_cmgl_response(list_buf, indices, &count, 32); // need parse function exported or duplicated
        // For simplicity, we'll re-implement a simple parser here (or better, expose driver function).
        // We'll add a helper in driver to extract indices, but to keep separation we'll implement a simple version.
        // Actually we can just loop through possible indices? Not efficient.
        // We'll rely on driver's list_sms and then parse manually.

        // Simple parsing: look for +CMGL: lines
        const char *p = list_buf;
        while ((p = strstr(p, "+CMGL:")) != NULL) {
            p += 6;
            int idx = atoi(p);
            if (idx > 0) {
                gsm_raw_sms_t raw;
                if (gsm_driver_read_sms(idx, &raw) == ESP_OK) {
                    process_sms(&raw);
                    gsm_driver_delete_sms(idx); // delete after processing
                }
            }
            // move to next line
            p = strchr(p, '\n');
            if (!p) break;
            p++;
        }
    }
    vTaskDelete(NULL);
}

static void send_sms_task(void *arg)
{
    pending_sms_t sms;
    int consecutive_fail = 0;
    while (s_ctx.running) {
        if (xQueueReceive(s_ctx.send_queue, &sms, portMAX_DELAY) == pdTRUE) {
            // Wait for network registration (up to 60 seconds)
            int retries = 0;
            while (!gsm_driver_is_registered() && retries < 12) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                retries++;
                consecutive_fail++;
                if (consecutive_fail >= 4) {
                    ESP_LOGW(TAG, "Module unresponsive (%d fails), attempting reset", consecutive_fail);
                    gsm_driver_reset();
                    consecutive_fail = 0;
                    // Allow extra time for registration after reset
                    vTaskDelay(pdMS_TO_TICKS(15000));
                }
            }
            if (!gsm_driver_is_registered()) {
                ESP_LOGE(TAG, "Network not registered, SMS send failed");
                system_event_t ev = { .id = EVENT_GSM_SMS_FAILED, .data = { {0} } };
                service_post_event(&ev);
                continue;
            }
            consecutive_fail = 0;
            esp_err_t ret = gsm_driver_send_sms(sms.number, sms.message, 60000);
            system_event_t ev = {
                .id = (ret == ESP_OK) ? EVENT_GSM_SMS_SENT : EVENT_GSM_SMS_FAILED,
                .data = { {0} }
            };
            service_post_event(&ev);
        }
    }
    vTaskDelete(NULL);
}

// Keep-alive task function
static void keepalive_task(void *arg)
{
    int consecutive_fail = 0;
    while (s_ctx.running) {
        vTaskDelay(pdMS_TO_TICKS(30000)); // ping every 30 seconds

        if (!gsm_driver_ping()) {
            consecutive_fail++;
            ESP_LOGW(TAG, "Keep-alive ping failed (%d)", consecutive_fail);
            if (consecutive_fail >= 2) {
                ESP_LOGW(TAG, "Module unresponsive, attempting reset");
                gsm_driver_reset();
                consecutive_fail = 0;
                // After reset, allow time for network registration
                vTaskDelay(pdMS_TO_TICKS(15000));
            }
        } else {
            consecutive_fail = 0;
        }
    }
    vTaskDelete(NULL);
}

