/**
 * @file wifi_service.c
 * @brief Implementation of the WiFi Service.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - The service runs a single FreeRTOS task that processes a queue of
 *   commands (from the command router), driver events (from the abstraction),
 *   and retry timer expirations.
 * - A finite state machine controls connection attempts and reconnections.
 * - Reconnection uses exponential backoff with configurable limits.
 * - All events emitted to the core are POD structures copied from temporary data.
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#include "wifi_service.h"
#include "wifi_private.h"
#include "wifi_driver_abstraction.h"
#include "service_interfaces.h"   // provides service_register_command, service_post_event
#include "esp_log.h"
#include "command_params.h"
#include <string.h>

static const char* TAG = "wifi_service";

/* Default configuration values (used if no command overrides) */
#define WIFI_DEFAULT_MIN_RETRY_MS   1000
#define WIFI_DEFAULT_MAX_RETRY_MS   60000
#define WIFI_DEFAULT_MAX_ATTEMPTS    5

/* Static service context (single instance) */
static struct wifi_service_context s_ctx = {0};

/* Forward declarations of internal functions */
static void wifi_service_task(void *pvParameters);
static void process_command(const command_msg_t *cmd);
static void process_driver_event(const driver_event_msg_t *evt);
static void process_retry(void);
static void set_state(wifi_service_state_t new_state);
static void emit_event(system_event_id_t event, void *data);
static void start_reconnect_timer(void);
static void stop_reconnect_timer(void);
static void reconnect_timer_callback(void *arg);
static void driver_event_callback(wifi_driver_event_t event, void *data);

/* Command handlers (registered with command router) */
static esp_err_t cmd_connect_wifi_handler(void *context, void *params);
static esp_err_t cmd_disconnect_wifi_handler(void *context, void *params);
static esp_err_t cmd_set_config_wifi_handler(void *context, void *params);
static esp_err_t cmd_get_status_wifi_handler(void *context, void *params);
static esp_err_t cmd_scan_wifi_handler(void *context, void *params);

/*============================================================================
 * Public API (service manager lifecycle)
 *============================================================================*/

esp_err_t wifi_service_init(void)
{
    /* Prevent double initialization */
    if (s_ctx.queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Create command/event queue */
    s_ctx.queue = xQueueCreate(10, sizeof(queue_item_t));
    if (!s_ctx.queue) {
        return ESP_ERR_NO_MEM;
    }

    /* Create reconnect timer */
    esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_callback,
        .arg = NULL,
        .name = "wifi_retry"
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_ctx.retry_timer);
    if (err != ESP_OK) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    /* Set default configuration */
    s_ctx.config.min_retry_delay_ms = WIFI_DEFAULT_MIN_RETRY_MS;
    s_ctx.config.max_retry_delay_ms = WIFI_DEFAULT_MAX_RETRY_MS;
    s_ctx.config.max_retry_attempts = WIFI_DEFAULT_MAX_ATTEMPTS;

    /* Initialize driver */
    err = wifi_driver_init();
    if (err != ESP_OK) {
        esp_timer_delete(s_ctx.retry_timer);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    /* Register driver callback */
    wifi_driver_register_callback(driver_event_callback);

    /* Start driver */
    err = wifi_driver_start();
    if (err != ESP_OK) {
        wifi_driver_register_callback(NULL);
        esp_timer_delete(s_ctx.retry_timer);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    /* Create service task */
    BaseType_t ret = xTaskCreate(wifi_service_task, "wifi_svc", 4096, NULL, 5, &s_ctx.task);
    if (ret != pdPASS) {
        wifi_driver_register_callback(NULL);
        wifi_driver_stop();
        esp_timer_delete(s_ctx.retry_timer);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ctx.state = WIFI_STATE_IDLE;
    ESP_LOGI(TAG, "WiFi service initialised");
    return ESP_OK;
}

esp_err_t wifi_service_register_handlers(void)
{
    esp_err_t ret;

    ret = service_register_command(CMD_CONNECT_WIFI, cmd_connect_wifi_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_DISCONNECT_WIFI, cmd_disconnect_wifi_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_SET_CONFIG_WIFI, cmd_set_config_wifi_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_GET_STATUS_WIFI, cmd_get_status_wifi_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_SCAN_WIFI, cmd_scan_wifi_handler, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "WiFi command handlers registered");
    return ESP_OK;
}

esp_err_t wifi_service_start(void)
{
    /* Driver already started in init; nothing to do */
    ESP_LOGI(TAG, "WiFi service started");
    return ESP_OK;
}

/*============================================================================
 * Command handlers (called by command router)
 *============================================================================*/

static esp_err_t cmd_connect_wifi_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_CONNECT_WIFI;
    memcpy(&item.msg.cmd.data.connect, params, sizeof(cmd_connect_wifi_params_t));

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_disconnect_wifi_handler(void *context, void *params)
{
    (void)context;
    (void)params;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_DISCONNECT_WIFI;

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_set_config_wifi_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_SET_CONFIG_WIFI;
    memcpy(&item.msg.cmd.data.set_config, params, sizeof(cmd_set_config_wifi_params_t));

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_get_status_wifi_handler(void *context, void *params)
{
    (void)context;
    (void)params;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_GET_STATUS_WIFI;

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_scan_wifi_handler(void *context, void *params)
{
    (void)context;
    (void)params;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_SCAN_WIFI;

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*============================================================================
 * Service Task
 *============================================================================*/

static void wifi_service_task(void *pvParameters)
{
    (void)pvParameters;
    queue_item_t item;

    while (1) {
        if (xQueueReceive(s_ctx.queue, &item, portMAX_DELAY) == pdTRUE) {
            switch (item.type) {
                case QUEUE_MSG_COMMAND:
                    process_command(&item.msg.cmd);
                    break;
                case QUEUE_MSG_DRIVER_EVENT:
                    process_driver_event(&item.msg.drv_evt);
                    break;
                case QUEUE_MSG_RETRY:
                    process_retry();
                    break;
                default:
                    break;
            }
        }
    }
}

/*============================================================================
 * Command Processing
 *============================================================================*/

static void process_command(const command_msg_t *cmd)
{
    switch (cmd->cmd_id) {
        case CMD_CONNECT_WIFI: {
            const cmd_connect_wifi_params_t *conn = &cmd->data.connect;

            /* Only allowed from IDLE or FAILED */
            if (s_ctx.state != WIFI_STATE_IDLE && s_ctx.state != WIFI_STATE_FAILED) {
                ESP_LOGD(TAG, "Connect ignored, current state %d", s_ctx.state);
                return;
            }

            /* Copy credentials */
            strlcpy(s_ctx.current_ssid, conn->ssid, sizeof(s_ctx.current_ssid));
            strlcpy(s_ctx.current_password, conn->password, sizeof(s_ctx.current_password));

            /* Reset retry counters */
            s_ctx.retry_count = 0;
            s_ctx.retry_delay_ms = s_ctx.config.min_retry_delay_ms;

            set_state(WIFI_STATE_CONNECTING);
            emit_event(EVENT_WIFI_CONNECTING, NULL);

            esp_err_t err = wifi_driver_connect(s_ctx.current_ssid, s_ctx.current_password);
            if (err != ESP_OK) {
                s_ctx.retry_count++;
                set_state(WIFI_STATE_FAILED);
                wifi_event_connection_failed_t evt = { .attempts = s_ctx.retry_count };
                emit_event(EVENT_WIFI_CONNECTION_FAILED, &evt);
            }
            break;
        }

        case CMD_DISCONNECT_WIFI: {
            s_ctx.user_disconnect = true;

            if (s_ctx.state == WIFI_STATE_RECONNECT_WAIT) {
                stop_reconnect_timer();
                set_state(WIFI_STATE_IDLE);
            } else if (s_ctx.state == WIFI_STATE_CONNECTING ||
                       s_ctx.state == WIFI_STATE_CONNECTED) {
                wifi_driver_disconnect();
            } else if (s_ctx.state == WIFI_STATE_FAILED) {
                set_state(WIFI_STATE_IDLE);
            }
            break;
        }

        case CMD_SET_CONFIG_WIFI: {
            const cmd_set_config_wifi_params_t *cfg = &cmd->data.set_config;
            s_ctx.config.min_retry_delay_ms = cfg->min_retry_delay_ms;
            s_ctx.config.max_retry_delay_ms = cfg->max_retry_delay_ms;
            s_ctx.config.max_retry_attempts = cfg->max_retry_attempts;
            break;
        }

        case CMD_GET_STATUS_WIFI: {
            emit_event(EVENT_WIFI_STATUS, &s_ctx.state);
            break;
        }

        case CMD_SCAN_WIFI: {
            ESP_LOGW(TAG, "WiFi scan command not implemented");
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown command %d", cmd->cmd_id);
            break;
    }
}

/*============================================================================
 * Driver Event Processing
 *============================================================================*/

static void process_driver_event(const driver_event_msg_t *evt)
{
    switch (evt->event) {
        case WIFI_DRIVER_EVENT_CONNECTED:
            if (s_ctx.state == WIFI_STATE_CONNECTING) {
                set_state(WIFI_STATE_CONNECTED);
                emit_event(EVENT_WIFI_CONNECTED, NULL);
                s_ctx.retry_count = 0;
                s_ctx.retry_delay_ms = s_ctx.config.min_retry_delay_ms;
            }
            break;

        case WIFI_DRIVER_EVENT_GOT_IP:
            if (s_ctx.state == WIFI_STATE_CONNECTED) {
                emit_event(EVENT_WIFI_GOT_IP, (void*)&evt->data.ip_info);
            }
            break;

        case WIFI_DRIVER_EVENT_DISCONNECTED: {
            int reason = evt->data.disconnect_reason;
            wifi_event_disconnected_t disc_evt = { .reason = reason };
            emit_event(EVENT_WIFI_DISCONNECTED, &disc_evt);

            if (s_ctx.user_disconnect) {
                s_ctx.user_disconnect = false;
                set_state(WIFI_STATE_IDLE);
                return;
            }

            /* Automatic reconnect logic */
            if (s_ctx.state == WIFI_STATE_CONNECTING ||
                s_ctx.state == WIFI_STATE_CONNECTED ||
                s_ctx.state == WIFI_STATE_RECONNECT_WAIT) {

                s_ctx.retry_count++;

                /* Check max attempts (0 = infinite) */
                if (s_ctx.config.max_retry_attempts > 0 &&
                    s_ctx.retry_count > s_ctx.config.max_retry_attempts) {
                    set_state(WIFI_STATE_FAILED);
                    wifi_event_connection_failed_t fail_evt = { .attempts = s_ctx.retry_count };
                    emit_event(EVENT_WIFI_CONNECTION_FAILED, &fail_evt);
                    return;
                }

                start_reconnect_timer();
                set_state(WIFI_STATE_RECONNECT_WAIT);
            }
            break;
        }

        default:
            break;
    }
}

/*============================================================================
 * Reconnect Timer Processing
 *============================================================================*/

static void process_retry(void)
{
    if (s_ctx.state == WIFI_STATE_RECONNECT_WAIT) {
        set_state(WIFI_STATE_CONNECTING);
        emit_event(EVENT_WIFI_CONNECTING, NULL);

        esp_err_t err = wifi_driver_connect(s_ctx.current_ssid, s_ctx.current_password);
        if (err != ESP_OK) {
            /* Simulate disconnect to trigger retry logic */
            driver_event_msg_t fake_evt;
            fake_evt.event = WIFI_DRIVER_EVENT_DISCONNECTED;
            fake_evt.data.disconnect_reason = -1;
            process_driver_event(&fake_evt);
        }
    }
}

/*============================================================================
 * Reconnect Timer Helpers
 *============================================================================*/

static void start_reconnect_timer(void)
{
    uint32_t delay = s_ctx.retry_delay_ms;
    s_ctx.retry_delay_ms *= 2;
    if (s_ctx.retry_delay_ms > s_ctx.config.max_retry_delay_ms) {
        s_ctx.retry_delay_ms = s_ctx.config.max_retry_delay_ms;
    }

    esp_timer_stop(s_ctx.retry_timer);
    esp_timer_start_once(s_ctx.retry_timer, delay * 1000);
}

static void stop_reconnect_timer(void)
{
    esp_timer_stop(s_ctx.retry_timer);
}

static void reconnect_timer_callback(void *arg)
{
    (void)arg;
    queue_item_t item = { .type = QUEUE_MSG_RETRY };
    xQueueSend(s_ctx.queue, &item, 0);
}

/*============================================================================
 * Driver Event Callback (called from ESP event task)
 *============================================================================*/

static void driver_event_callback(wifi_driver_event_t event, void *data)
{
    if (!s_ctx.queue) {
        return;
    }

    queue_item_t item;
    item.type = QUEUE_MSG_DRIVER_EVENT;
    item.msg.drv_evt.event = event;

    if (event == WIFI_DRIVER_EVENT_DISCONNECTED && data) {
        item.msg.drv_evt.data.disconnect_reason = *(int*)data;
    } else if (event == WIFI_DRIVER_EVENT_GOT_IP && data) {
        memcpy(&item.msg.drv_evt.data.ip_info, data, sizeof(esp_netif_ip_info_t));
    }

    xQueueSend(s_ctx.queue, &item, 0);
}

/*============================================================================
 * State Management and Event Emission
 *============================================================================*/

static void set_state(wifi_service_state_t new_state)
{
    if (s_ctx.state != new_state) {
        s_ctx.state = new_state;
        ESP_LOGD(TAG, "State -> %d", new_state);
    }
}

static void emit_event(system_event_id_t event, void *data)
{
    system_event_t ev = {
        .id = event,
        .data = { {0} }
    };

    switch (event) {
        case EVENT_WIFI_DISCONNECTED:
            if (data) {
                wifi_event_disconnected_t *d = (wifi_event_disconnected_t*)data;
                ev.data.wifi_disconnected.reason = d->reason;
            }
            break;
        case EVENT_WIFI_CONNECTION_FAILED:
            if (data) {
                wifi_event_connection_failed_t *f = (wifi_event_connection_failed_t*)data;
                ev.data.wifi_connection_failed.attempts = f->attempts;
            }
            break;
        case EVENT_WIFI_STATUS:
            if (data) {
                /* data is pointer to wifi_service_state_t, cast to uint32_t */
                ev.data.status_value = (uint32_t)(*(wifi_service_state_t*)data);
            }
            break;
        case EVENT_WIFI_GOT_IP:
            if (data) {
                memcpy(&ev.data.ip_info, data, sizeof(esp_netif_ip_info_t));
            }
            break;
        default:
            /* No payload for other WiFi events */
            break;
    }

    service_post_event(&ev);
}