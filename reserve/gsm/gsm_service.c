/**
 * @file components/services/connectivity/gsm/gsm_service.c
 * @brief GSM Service – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Runs a single FreeRTOS task that processes:
 *   * Commands from the command router (via queue)
 *   * Incoming SMS (via driver polling)
 * - Maintains password and authorised numbers in static config.
 * - Posts EVENT_AUTH_GRANTED or EVENT_AUTH_DENIED based on SMS content.
 * - Sends SMS asynchronously using a command queue (non‑blocking for caller).
 *
 * @version 1.0.0
 * @author System Architecture Team
 * @date 2026
 */

#include "gsm_service.h"
#include "gsm_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "string.h"
#include <ctype.h>

static const char *TAG = "GSM_SVC";

/* ============================================================================
 * Configuration (temporary – will move to Kconfig)
 * ============================================================================ */
#define GSM_UART_NUM         UART_NUM_2
#define GSM_TX_PIN           GPIO_NUM_17
#define GSM_RX_PIN           GPIO_NUM_16
#define GSM_BAUD_RATE        9600
#define GSM_RX_BUFFER_SIZE   2048
#define GSM_CMD_TIMEOUT_MS   30000
#define GSM_RETRY_COUNT      2

#define GSM_SERVICE_STACK_SIZE 8192

#define SMS_POLL_INTERVAL_MS    2000   /* check for SMS every 2 seconds */
#define MAX_AUTHORIZED_NUMBERS  10
#define PASSWORD_MAX_LEN        33
#define PHONE_NUMBER_MAX_LEN    16

/* ============================================================================
 * Internal message types for the service queue
 * ============================================================================ */
typedef enum {
    QUEUE_MSG_SEND_SMS,          /* Send an SMS */
    QUEUE_MSG_SEND_SMS_RESPONSE, /* Send an SMS response (same as above) */
    QUEUE_MSG_SET_PASSWORD,      /* Set authentication password */
    QUEUE_MSG_SET_AUTHORIZED,    /* Set authorised numbers list */
    QUEUE_MSG_CONNECT,           /* Initialise driver and start polling */
    QUEUE_MSG_DISCONNECT,         /* Stop driver */
    QUEUE_MSG_SEND_NOTIFICATION,         /* send normal SMS (collector) */
    QUEUE_MSG_ESCALATE_NOTIFICATION,     /* send escalation SMS (manager) */
} internal_cmd_t;

typedef struct {
    internal_cmd_t cmd;
    union {
        struct {
            char phone[PHONE_NUMBER_MAX_LEN];
            char message[161];
        } send_sms;
        struct {
            char password[PASSWORD_MAX_LEN];
        } set_password;
        struct {
            char numbers[MAX_AUTHORIZED_NUMBERS][PHONE_NUMBER_MAX_LEN];
            uint8_t count;
        } set_authorized;
        struct {
            char message[161];   /* SMS text */
        } notification;
    } data;
} queue_item_t;

/* ============================================================================
 * Service context (static, single instance)
 * ============================================================================ */
typedef struct {
    TaskHandle_t task;               /* Service task handle */
    QueueHandle_t queue;             /* Internal command queue */
    gsm_handle_t driver;             /* GSM driver handle */
    bool running;                    /* True if polling active */
    char password[PASSWORD_MAX_LEN]; /* Authentication password */
    char authorized_numbers[MAX_AUTHORIZED_NUMBERS][PHONE_NUMBER_MAX_LEN];
    uint8_t authorized_count;        /* Number of authorised numbers */
    char collector_number[PHONE_NUMBER_MAX_LEN];   /* for near‑full/full alerts */
    char manager_number[PHONE_NUMBER_MAX_LEN];     /* for escalation alerts */
    bool driver_ready;               /* True after GSM initialised */
} gsm_service_ctx_t;

static gsm_service_ctx_t s_ctx = {0};

/* ============================================================================
 * Forward declarations
 * ============================================================================ */
static void gsm_service_task(void *pvParameters);
static void process_incoming_sms(void);
static void process_queue_item(const queue_item_t *item);
static bool is_authorized(const char *phone_number);
static bool check_password(const char *message);
static void cleanup_phone_number(char *phone);

/* ============================================================================
 * Command handlers (registered with command router)
 * ============================================================================ */
static esp_err_t cmd_send_sms_generic_handler(void *context, void *params);
static esp_err_t cmd_set_gsm_password_handler(void *context, void *params);
static esp_err_t cmd_set_authorized_numbers_handler(void *context, void *params);
static esp_err_t cmd_connect_gsm_handler(void *context, void *params);
static esp_err_t cmd_disconnect_gsm_handler(void *context, void *params);

/* ============================================================================
 * Helper: clean phone number (keep only digits and '+')
 * ============================================================================ */
static void cleanup_phone_number(char *phone)
{
    if (!phone) return;
    char *src = phone;
    char *dst = phone;
    while (*src) {
        if (isdigit((unsigned char)*src) || *src == '+') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

/* ============================================================================
 * Check if phone number is authorised
 * ============================================================================ */
static bool is_authorized(const char *phone_number)
{
    if (s_ctx.authorized_count == 0) {
        return true; /* No list means all numbers allowed */
    }
    char cleaned[PHONE_NUMBER_MAX_LEN];
    strlcpy(cleaned, phone_number, sizeof(cleaned));
    cleanup_phone_number(cleaned);

    for (int i = 0; i < s_ctx.authorized_count; i++) {
        char auth_cleaned[PHONE_NUMBER_MAX_LEN];
        strlcpy(auth_cleaned, s_ctx.authorized_numbers[i], sizeof(auth_cleaned));
        cleanup_phone_number(auth_cleaned);
        if (strcmp(cleaned, auth_cleaned) == 0) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * Check if message contains the correct password
 * ============================================================================ */
static bool check_password(const char *message)
{
    if (s_ctx.password[0] == '\0') {
        return true; /* No password set → always accept */
    }
    return (strstr(message, s_ctx.password) != NULL);
}

/* Periodic SMS check using AT+CMGL (like old working code) */
static void check_for_sms_periodic(void)
{
    
    // ESP_LOGI(TAG, "check_for_sms_periodic() called, driver_ready=%d", s_ctx.driver_ready); /**< Debug log for GSM */

    if (!s_ctx.driver_ready || !s_ctx.running) return;

    /* Send AT+CMGL="ALL" to list all SMS */
    char response[1024];
    esp_err_t ret = gsm_driver_send_at(s_ctx.driver, "AT+CMGL=\"ALL\"", "OK", response, sizeof(response), 10000);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "No SMS found or command failed");
        return;
    }

    /* Parse all +CMGL: lines to get SMS indices */
    char *ptr = response;
    int sms_processed = 0;

    while ((ptr = strstr(ptr, "+CMGL:")) != NULL) {
        /* Extract index: +CMGL: <index>,... */
        int sms_index = -1;
        if (sscanf(ptr, "+CMGL: %d,", &sms_index) == 1) {
            if (sms_index > 0) {
                ESP_LOGI(TAG, "Found SMS at index %d, reading...", sms_index);
                
                /* Read SMS using AT+CMGR */
                char cmd[32];
                snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", sms_index);
                char sms_response[512];
                ret = gsm_driver_send_at(s_ctx.driver, cmd, "+CMGR:", sms_response, sizeof(sms_response), 5000);
                if (ret == ESP_OK) {
                    /* Parse sender and message (same parser as before) */
                    char sender[16] = {0};
                    char message[161] = {0};
                    
                    char *cmgr = strstr(sms_response, "+CMGR:");
                    if (cmgr) {
                        char *q1 = strchr(cmgr, '"');
                        if (q1) q1 = strchr(q1 + 1, '"');
                        if (q1) {
                            char *q2 = strchr(q1 + 1, '"');
                            if (q2) {
                                char *q3 = strchr(q2 + 1, '"');
                                if (q3) {
                                    size_t len = q3 - q2 - 1;
                                    if (len > sizeof(sender)-1) len = sizeof(sender)-1;
                                    strncpy(sender, q2 + 1, len);
                                    sender[len] = '\0';
                                }
                            }
                        }
                        /* Message after timestamp */
                        char *msg_start = strstr(cmgr, "\r\n");
                        if (msg_start) {
                            msg_start += 2;
                            char *msg_end = strstr(msg_start, "\r\n");
                            if (!msg_end) msg_end = msg_start + strlen(msg_start);
                            size_t msg_len = msg_end - msg_start;
                            if (msg_len > sizeof(message)-1) msg_len = sizeof(message)-1;
                            strncpy(message, msg_start, msg_len);
                            message[msg_len] = '\0';
                        }
                    }
                    
                    ESP_LOGI(TAG, "Received SMS from %s: %s", sender, message);
                    
                    /* Delete the SMS after reading */
                    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", sms_index);
                    gsm_driver_send_at(s_ctx.driver, cmd, "OK", NULL, 0, 3000);
                    
                    /* Authentication and event posting */
                    bool auth = is_authorized(sender) && check_password(message);
                    system_event_t ev = {
                        .id = auth ? EVENT_AUTH_GRANTED : EVENT_AUTH_DENIED,
                        .timestamp_us = esp_timer_get_time(),
                        .source = 0,
                        .data = { { {0} } }
                    };
                    strlcpy(ev.data.gsm_command.sender, sender, sizeof(ev.data.gsm_command.sender));
                    if (auth) {
                        strlcpy(ev.data.gsm_command.command, "MAINTENANCE", sizeof(ev.data.gsm_command.command));
                    }
                    service_post_event(&ev);
                    
                    if (auth) ESP_LOGI(TAG, "Authentication GRANTED for %s", sender);
                    else ESP_LOGW(TAG, "Authentication DENIED for %s", sender);
                    
                    sms_processed++;
                }
            }
        }
        ptr++; /* move forward */
    }
    
    if (sms_processed > 0) {
        ESP_LOGI(TAG, "Processed %d SMS messages", sms_processed);
    }
}

/* ============================================================================
 * Process an incoming SMS (called from service task)
 * ============================================================================ */
static void process_incoming_sms(void)
{
    if (!s_ctx.driver_ready || !s_ctx.running) return;

    char line[256];
    esp_err_t ret;

    /* Look for +CMT: line (new SMS notification) */
    ret = gsm_driver_read_line(s_ctx.driver, line, sizeof(line), 500);
    if (ret != ESP_OK) return;

    ESP_LOGD(TAG, "Raw line: %s", line);
    if (strstr(line, "+CMT:") == NULL) return;

    /* Now read the actual SMS content – it comes on the next line */
    char sms_line[256];
    ret = gsm_driver_read_line(s_ctx.driver, sms_line, sizeof(sms_line), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read SMS content");
        return;
    }

    ESP_LOGI(TAG, "SMS content: %s", sms_line);

    /* === Parse sender and message (simple method that works) === */
    /* Format: "+255688173415","","25/03/15,10:30:00+12"  and the message is on the next line? 
       Actually, the message may be part of the same line after the timestamp. 
       In your old code, you used AT+CMGR to read by index. Let's use that method because it's reliable. */

    /* Instead of parsing the +CMT: line, we will read the SMS index and use AT+CMGR */
    /* Extract the index from +CMT: line – format: +CMT: "SM",<index> */
    int sms_index = -1;
    if (sscanf(line, "+CMT: \"SM\",%d", &sms_index) != 1) {
        /* Try alternative format */
        char *p = strstr(line, "+CMT:");
        if (p) {
            p = strchr(p, ',');
            if (p) sms_index = atoi(p + 1);
        }
    }
    if (sms_index <= 0) {
        ESP_LOGW(TAG, "Could not parse SMS index");
        return;
    }

    ESP_LOGI(TAG, "New SMS at index %d, reading...", sms_index);

    /* Use AT+CMGR to read the SMS by index */
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", sms_index);
    char response[512];
    ret = gsm_driver_send_at(s_ctx.driver, cmd, "+CMGR:", response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SMS index %d", sms_index);
        return;
    }

    /* Parse the response: find sender and message */
    char sender[16] = {0};
    char message[161] = {0};

    /* Look for +CMGR: line, e.g., +CMGR: "REC READ","+255688173415","","25/03/15,10:30:00" */
    char *cmgr = strstr(response, "+CMGR:");
    if (!cmgr) {
        ESP_LOGE(TAG, "No +CMGR in response");
        return;
    }

    /* Find first quote after cmgr */
    char *q1 = strchr(cmgr, '"');
    if (!q1) return;
    q1 = strchr(q1 + 1, '"');  /* skip status */
    if (!q1) return;
    char *q2 = strchr(q1 + 1, '"');  /* start of sender */
    if (!q2) return;
    char *q3 = strchr(q2 + 1, '"');  /* end of sender */
    if (!q3) return;

    size_t sender_len = q3 - q2 - 1;
    if (sender_len > sizeof(sender) - 1) sender_len = sizeof(sender) - 1;
    strncpy(sender, q2 + 1, sender_len);
    sender[sender_len] = '\0';

    /* Message is after the timestamp – locate the end of the header */
    char *msg_start = strstr(q3, "\r\n");
    if (!msg_start) {
        msg_start = strstr(q3, "\n");
    }
    if (msg_start) {
        msg_start += 2; /* skip \r\n */
        /* Remove trailing \r\n */
        char *msg_end = strstr(msg_start, "\r\n");
        if (!msg_end) msg_end = msg_start + strlen(msg_start);
        size_t msg_len = msg_end - msg_start;
        if (msg_len > sizeof(message) - 1) msg_len = sizeof(message) - 1;
        strncpy(message, msg_start, msg_len);
        message[msg_len] = '\0';
    }

    ESP_LOGI(TAG, "Received SMS from %s: %s", sender, message);

    /* Delete the SMS after reading */
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", sms_index);
    gsm_driver_send_at(s_ctx.driver, cmd, "OK", NULL, 0, 3000);

    /* --- Authentication decision --- */
    bool auth = is_authorized(sender) && check_password(message);

    /* Post event to core */
    system_event_t ev = {
        .id = auth ? EVENT_AUTH_GRANTED : EVENT_AUTH_DENIED,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    strlcpy(ev.data.gsm_command.sender, sender, sizeof(ev.data.gsm_command.sender));
    if (auth) {
        strlcpy(ev.data.gsm_command.command, "MAINTENANCE", sizeof(ev.data.gsm_command.command));
    }
    service_post_event(&ev);

    if (auth) {
        ESP_LOGI(TAG, "Authentication GRANTED for %s", sender);
    } else {
        ESP_LOGW(TAG, "Authentication DENIED for %s", sender);
    }
}

/* ============================================================================
 * Service task – processes commands and polls for SMS
 * ============================================================================ */
static void gsm_service_task(void *pvParameters)
{
    (void)pvParameters;
    queue_item_t item;

    TickType_t last_poll = xTaskGetTickCount();

    while (1) 
    {
        static uint32_t loop_count = 0;
        if (++loop_count % 100 == 0) ESP_LOGI(TAG, "Service task alive, loop %" PRIu32, loop_count);

        /* Process queue items (non‑blocking) */
        while (xQueueReceive(s_ctx.queue, &item, 0) == pdTRUE) {
            process_queue_item(&item);
        }

        /* Poll for incoming SMS periodically */
        if (s_ctx.running && s_ctx.driver_ready) {
            #if 1
                check_for_sms_periodic();
            #else
                process_incoming_sms();
            #endif
        }

        /* Wait a bit – use vTaskDelayUntil for deterministic period */
        vTaskDelayUntil(&last_poll, pdMS_TO_TICKS(SMS_POLL_INTERVAL_MS / 10));
        /* Actually we want a short loop to keep responsiveness, but to avoid CPU hog,
           we'll use a small fixed delay. */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ============================================================================
 * Process a queue item (internal command)
 * ============================================================================ */
static void process_queue_item(const queue_item_t *item)
{
    switch (item->cmd) {
        case QUEUE_MSG_SEND_SMS:
        case QUEUE_MSG_SEND_SMS_RESPONSE:
            if (!s_ctx.driver_ready) {
                ESP_LOGW(TAG, "GSM not ready, cannot send SMS");
                return;
            }
            esp_err_t ret = gsm_driver_send_sms(s_ctx.driver,
                                                item->data.send_sms.phone,
                                                item->data.send_sms.message);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "SMS sent to %s", item->data.send_sms.phone);
            } else {
                ESP_LOGE(TAG, "Failed to send SMS to %s", item->data.send_sms.phone);
            }
            break;

        case QUEUE_MSG_SET_PASSWORD:
            strlcpy(s_ctx.password, item->data.set_password.password, sizeof(s_ctx.password));
            ESP_LOGI(TAG, "GSM password updated");
            break;

        case QUEUE_MSG_SET_AUTHORIZED:
            s_ctx.authorized_count = item->data.set_authorized.count;
            for (int i = 0; i < s_ctx.authorized_count; i++) {
                strlcpy(s_ctx.authorized_numbers[i],
                        item->data.set_authorized.numbers[i],
                        sizeof(s_ctx.authorized_numbers[i]));
            }
            ESP_LOGI(TAG, "Authorised numbers updated (%d entries)", s_ctx.authorized_count);
            break;

        case QUEUE_MSG_CONNECT:
            if (s_ctx.driver_ready) {
                ESP_LOGW(TAG, "GSM already connected");
                return;
            }

            ESP_LOGI(TAG, "Processing QUEUE_MSG_CONNECT, driver_ready=%d", s_ctx.driver_ready); /* Debug log to verify we are in the connect case */

            /* Initialise driver */
            gsm_driver_config_t cfg = {
                .uart_num = GSM_UART_NUM,
                .tx_pin = GSM_TX_PIN,
                .rx_pin = GSM_RX_PIN,
                .baud_rate = GSM_BAUD_RATE,
                .rx_buffer_size = GSM_RX_BUFFER_SIZE,
                .cmd_timeout_ms = GSM_CMD_TIMEOUT_MS,
                .retry_count = GSM_RETRY_COUNT,
            };
            ret = gsm_driver_create(&cfg, &s_ctx.driver);
            if (ret == ESP_OK) {
                s_ctx.driver_ready = true;
                ESP_LOGI(TAG, "GSM connected");
                ESP_LOGI(TAG, "gsm_driver_create succeeded, driver_ready set to true");
            } else {
                ESP_LOGE(TAG, "GSM connect failed: %d", ret);
            }
            break;

        case QUEUE_MSG_DISCONNECT:
            if (s_ctx.driver_ready) {
                gsm_driver_delete(s_ctx.driver);
                s_ctx.driver = NULL;
                s_ctx.driver_ready = false;
                ESP_LOGI(TAG, "GSM disconnected");
            }
            break;

        case QUEUE_MSG_SEND_NOTIFICATION:
            if (!s_ctx.driver_ready) {
                ESP_LOGW(TAG, "GSM not ready, cannot send notification");
                break;
            }
            if (s_ctx.collector_number[0] == '\0') {
                ESP_LOGW(TAG, "Collector number not set, cannot send notification");
                break;
            }
            ret = gsm_driver_send_sms(s_ctx.driver,
                                                s_ctx.collector_number,
                                                item->data.notification.message);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Notification sent to collector: %s", item->data.notification.message);
            } else {
                ESP_LOGE(TAG, "Failed to send notification");
            }
            break;

        case QUEUE_MSG_ESCALATE_NOTIFICATION:
            if (!s_ctx.driver_ready) {
                ESP_LOGW(TAG, "GSM not ready, cannot send escalation");
                break;
            }
            if (s_ctx.manager_number[0] == '\0') {
                ESP_LOGW(TAG, "Manager number not set, cannot send escalation");
                break;
            }
            ret = gsm_driver_send_sms(s_ctx.driver,
                                    s_ctx.manager_number,
                                    item->data.notification.message);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Escalation sent to manager: %s", item->data.notification.message);
            } else {
                ESP_LOGE(TAG, "Failed to send escalation");
            }
            break;
    }
}

/* Single handler for both CMD_SEND_SMS and CMD_SEND_SMS_RESPONSE */
static esp_err_t cmd_send_sms_generic_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const cmd_send_sms_params_t *p = (const cmd_send_sms_params_t *)params;

    queue_item_t item = { .cmd = QUEUE_MSG_SEND_SMS };
    strlcpy(item.data.send_sms.phone, p->phone_number, sizeof(item.data.send_sms.phone));
    strlcpy(item.data.send_sms.message, p->message, sizeof(item.data.send_sms.message));
    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) return ESP_FAIL;
    return ESP_OK;
}

/* Handler for CMD_SEND_NOTIFICATION */
static esp_err_t cmd_send_notification_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const cmd_send_notification_params_t *p = (const cmd_send_notification_params_t *)params;

    queue_item_t item = { .cmd = QUEUE_MSG_SEND_NOTIFICATION };
    snprintf(item.data.notification.message, sizeof(item.data.notification.message),
             "%s", p->message);
    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) return ESP_FAIL;
    return ESP_OK;
}

/* Handler for CMD_ESCALATE_NOTIFICATION (or CMD_SEND_ESCALATION_NOTIFICATION) */
static esp_err_t cmd_escalate_notification_handler(void *context, void *params)
{
    (void)context;
    (void)params;   /* no parameters expected, or use default message */
    queue_item_t item = { .cmd = QUEUE_MSG_ESCALATE_NOTIFICATION };
    /* You can also accept an optional message via params */
    const char *default_msg = "URGENT: Bin full, collector did not respond within 1 hour.";
    strlcpy(item.data.notification.message, default_msg, sizeof(item.data.notification.message));
    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t cmd_set_gsm_password_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const char *password = (const char *)params; // as per command_params.h
    queue_item_t item = { .cmd = QUEUE_MSG_SET_PASSWORD };
    strlcpy(item.data.set_password.password, password, sizeof(item.data.set_password.password));
    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t cmd_set_authorized_numbers_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    // The parameter type is an array of strings; we'll assume a simple structure
    // In command_params.h, it's `char **` or similar. For simplicity, we accept a pointer to a struct.
    // Since the exact type is not fully defined, we will cast carefully.
    // The documentation says: "params: array" – we assume a null‑terminated list.
    const char **numbers = (const char **)params;
    queue_item_t item = { .cmd = QUEUE_MSG_SET_AUTHORIZED };
    uint8_t count = 0;
    while (numbers[count] != NULL && count < MAX_AUTHORIZED_NUMBERS) {
        strlcpy(item.data.set_authorized.numbers[count], numbers[count],
                sizeof(item.data.set_authorized.numbers[count]));
        count++;
    }
    item.data.set_authorized.count = count;
    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t cmd_connect_gsm_handler(void *context, void *params)
{
    (void)context;
    (void)params;
    queue_item_t item = { .cmd = QUEUE_MSG_CONNECT };
    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t cmd_disconnect_gsm_handler(void *context, void *params)
{
    (void)context;
    (void)params;
    queue_item_t item = { .cmd = QUEUE_MSG_DISCONNECT };
    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) return ESP_FAIL;
    return ESP_OK;
}

/* ============================================================================
 * Public API (service manager lifecycle)
 * ============================================================================ */
esp_err_t gsm_service_init(void)
{
    /* Default phone numbers for notifications – can be updated via commands */
    strlcpy(s_ctx.collector_number, "+255688173415", sizeof(s_ctx.collector_number));
    strlcpy(s_ctx.manager_number,   "+255740073415", sizeof(s_ctx.manager_number));

    /* Default password for authentication */
    strlcpy(s_ctx.password, "SECRET123", sizeof(s_ctx.password));

    if (s_ctx.queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.queue = xQueueCreate(10, sizeof(queue_item_t));
    if (!s_ctx.queue) {
        return ESP_ERR_NO_MEM;
    }

    s_ctx.running = false;
    s_ctx.driver_ready = false;
    s_ctx.password[0] = '\0';
    s_ctx.authorized_count = 0;

    /* Create service task (initially not running) */
    BaseType_t ret = xTaskCreate(gsm_service_task, "gsm_svc", GSM_SERVICE_STACK_SIZE, NULL, 5, &s_ctx.task);
    if (ret != pdPASS) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GSM service initialised");
    return ESP_OK;
}

esp_err_t gsm_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_SEND_SMS, cmd_send_sms_generic_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_SEND_SMS_RESPONSE, cmd_send_sms_generic_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_SET_GSM_PASSWORD, cmd_set_gsm_password_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_SET_AUTHORIZED_NUMBERS, cmd_set_authorized_numbers_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_CONNECT_GSM, cmd_connect_gsm_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_DISCONNECT_GSM, cmd_disconnect_gsm_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_SEND_NOTIFICATION, cmd_send_notification_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_ESCALATE_NOTIFICATION, cmd_escalate_notification_handler, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "GSM command handlers registered");
    return ESP_OK;
}

esp_err_t gsm_service_start(void)
{
    s_ctx.running = true;
    queue_item_t conn_item = { .cmd = QUEUE_MSG_CONNECT };
    xQueueSend(s_ctx.queue, &conn_item, 0);
    ESP_LOGI(TAG, "GSM service started");
    return ESP_OK;
}

esp_err_t gsm_service_stop(void)
{
    s_ctx.running = false;
    if (s_ctx.driver_ready) {
        gsm_driver_delete(s_ctx.driver);
        s_ctx.driver = NULL;
        s_ctx.driver_ready = false;
    }
    ESP_LOGI(TAG, "GSM service stopped");
    return ESP_OK;
}