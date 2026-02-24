/**
 * @file mqtt_service.c
 * @brief Implementation of the MQTT Service.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - The service runs a single FreeRTOS task that processes a queue of
 *   commands (from the command router), client events (from the abstraction),
 *   and retry timer expirations.
 * - A finite state machine controls connection attempts, reconnections,
 *   and interaction with the WiFi status.
 * - Reconnection uses exponential backoff with configurable limits.
 * - All events emitted to the core are POD structures copied from temporary data.
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#include "mqtt_service.h"
#include "mqtt_private.h"
#include "mqtt_client_abstraction.h"
#include "mqtt_topic.h"
#include "service_interfaces.h"
#include "esp_log.h"
#include "command_params.h"
#include <string.h>

static const char* TAG = "mqtt_service";

/* Default configuration values (used if no command overrides) */
#define MQTT_DEFAULT_MIN_RETRY_MS   2000
#define MQTT_DEFAULT_MAX_RETRY_MS   60000
#define MQTT_DEFAULT_MAX_ATTEMPTS    5
#define MQTT_DEFAULT_KEEPALIVE       120

/* Static service context (single instance) */
static struct mqtt_service_context s_ctx = {0};

/* Forward declarations of internal functions */
static void mqtt_service_task(void *pvParameters);
static void process_command(const command_msg_t *cmd);
static void process_client_event(const client_event_msg_t *evt);
static void process_retry(void);
static void set_state(mqtt_service_state_t new_state);
static void emit_event(system_event_id_t event, void *data);
static void start_reconnect_timer(void);
static void stop_reconnect_timer(void);
static void reconnect_timer_callback(void *arg);
static void client_event_callback(mqtt_client_event_t event, void *data);

/* Command handlers (registered with command router) */
static esp_err_t cmd_connect_mqtt_handler(void *context, void *params);
static esp_err_t cmd_disconnect_mqtt_handler(void *context, void *params);
static esp_err_t cmd_set_config_mqtt_handler(void *context, void *params);
static esp_err_t cmd_publish_mqtt_handler(void *context, void *params);
static esp_err_t cmd_subscribe_mqtt_handler(void *context, void *params);
static esp_err_t cmd_unsubscribe_mqtt_handler(void *context, void *params);
static esp_err_t cmd_set_wifi_state_handler(void *context, void *params);

/*============================================================================
 * Public API (service manager lifecycle)
 *============================================================================*/

esp_err_t mqtt_service_init(void)
{
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
        .name = "mqtt_retry"
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_ctx.retry_timer);
    if (err != ESP_OK) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    /* Set default configuration */
    s_ctx.config.min_retry_delay_ms = MQTT_DEFAULT_MIN_RETRY_MS;
    s_ctx.config.max_retry_delay_ms = MQTT_DEFAULT_MAX_RETRY_MS;
    s_ctx.config.max_retry_attempts = MQTT_DEFAULT_MAX_ATTEMPTS;
    s_ctx.config.keepalive = MQTT_DEFAULT_KEEPALIVE;
    /* other strings remain empty (will be filled later) */

    /* Initialize client abstraction with default config (will be reconfigured later) */
    mqtt_client_config_t client_cfg = {
        .broker_uri = "mqtt://test.mosquitto.org:1883",
        .client_id = NULL,
        .username = NULL,
        .password = NULL,
        .keepalive = MQTT_DEFAULT_KEEPALIVE,
        .disable_clean_session = false,
        .lwt_qos = 0,
        .lwt_retain = false,
        .lwt_topic = NULL,
        .lwt_message = NULL,
        .task_stack_size = 0,
        .task_priority = 0
    };
    err = mqtt_client_init(&client_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init MQTT client: %s", esp_err_to_name(err));
        esp_timer_delete(s_ctx.retry_timer);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    /* Register client callback */
    mqtt_client_register_callback(client_event_callback);

    /* Create service task */
    BaseType_t ret = xTaskCreate(mqtt_service_task, "mqtt_svc", 4096, NULL, 5, &s_ctx.task);
    if (ret != pdPASS) {
        mqtt_client_stop();
        esp_timer_delete(s_ctx.retry_timer);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ctx.state = MQTT_STATE_IDLE;
    s_ctx.wifi_connected = false;   /* assume WiFi not connected initially */
    ESP_LOGI(TAG, "MQTT service initialised");
    return ESP_OK;
}

esp_err_t mqtt_service_register_handlers(void)
{
    esp_err_t ret;

    ret = service_register_command(CMD_CONNECT_MQTT, cmd_connect_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_DISCONNECT_MQTT, cmd_disconnect_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_SET_CONFIG_MQTT, cmd_set_config_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_PUBLISH_MQTT, cmd_publish_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_SUBSCRIBE_MQTT, cmd_subscribe_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_UNSUBSCRIBE_MQTT, cmd_unsubscribe_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_MQTT_SET_WIFI_STATE, cmd_set_wifi_state_handler, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "MQTT command handlers registered");
    return ESP_OK;
}

esp_err_t mqtt_service_start(void)
{
    ESP_LOGI(TAG, "MQTT service started");
    return ESP_OK;
}

/*============================================================================
 * Command handlers (called by command router)
 *============================================================================*/

static esp_err_t cmd_set_wifi_state_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    uint32_t state = *(uint32_t*)params;
    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_MQTT_SET_WIFI_STATE;
    item.msg.cmd.data.wifi_state = state;

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_connect_mqtt_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_CONNECT_MQTT;
    memcpy(&item.msg.cmd.data.connect, params, sizeof(cmd_connect_mqtt_params_t));

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_disconnect_mqtt_handler(void *context, void *params)
{
    (void)context;
    (void)params;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_DISCONNECT_MQTT;

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_set_config_mqtt_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_SET_CONFIG_MQTT;
    memcpy(&item.msg.cmd.data.set_config, params, sizeof(cmd_set_config_mqtt_params_t));

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_publish_mqtt_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_PUBLISH_MQTT;
    memcpy(&item.msg.cmd.data.publish, params, sizeof(cmd_publish_mqtt_params_t));

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_subscribe_mqtt_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_SUBSCRIBE_MQTT;
    memcpy(&item.msg.cmd.data.subscribe, params, sizeof(cmd_subscribe_mqtt_params_t));

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cmd_unsubscribe_mqtt_handler(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = CMD_UNSUBSCRIBE_MQTT;
    memcpy(&item.msg.cmd.data.unsubscribe, params, sizeof(cmd_unsubscribe_mqtt_params_t));

    if (xQueueSend(s_ctx.queue, &item, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*============================================================================
 * Service Task
 *============================================================================*/

static void mqtt_service_task(void *pvParameters)
{
    (void)pvParameters;
    queue_item_t item;

    while (1) {
        if (xQueueReceive(s_ctx.queue, &item, portMAX_DELAY) == pdTRUE) {
            switch (item.type) {
                case QUEUE_MSG_COMMAND:
                    process_command(&item.msg.cmd);
                    break;
                case QUEUE_MSG_CLIENT_EVENT:
                    process_client_event(&item.msg.client_evt);
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
        case CMD_CONNECT_MQTT: {
            const cmd_connect_mqtt_params_t *p = &cmd->data.connect;

            /* Copy config from connect command */
            strlcpy(s_ctx.config.broker_uri, p->broker_uri, sizeof(s_ctx.config.broker_uri));
            strlcpy(s_ctx.config.client_id, p->client_id, sizeof(s_ctx.config.client_id));
            strlcpy(s_ctx.config.username, p->username, sizeof(s_ctx.config.username));
            strlcpy(s_ctx.config.password, p->password, sizeof(s_ctx.config.password));
            s_ctx.config.keepalive = p->keepalive;
            s_ctx.config.disable_clean_session = p->disable_clean_session;
            s_ctx.config.lwt_qos = p->lwt_qos;
            s_ctx.config.lwt_retain = p->lwt_retain;
            strlcpy(s_ctx.config.lwt_topic, p->lwt_topic, sizeof(s_ctx.config.lwt_topic));
            strlcpy(s_ctx.config.lwt_message, p->lwt_message, sizeof(s_ctx.config.lwt_message));
            s_ctx.config.min_retry_delay_ms = p->min_retry_delay_ms;
            s_ctx.config.max_retry_delay_ms = p->max_retry_delay_ms;
            s_ctx.config.max_retry_attempts = p->max_retry_attempts;

            /* Only allow connect from IDLE or FAILED */
            if (s_ctx.state != MQTT_STATE_IDLE && s_ctx.state != MQTT_STATE_FAILED) {
                ESP_LOGD(TAG, "Connect ignored, current state %d", s_ctx.state);
                return;
            }

            /* If WiFi not connected, stay in IDLE (will be triggered when WiFi becomes available) */
            if (!s_ctx.wifi_connected) {
                ESP_LOGD(TAG, "WiFi not connected, deferring MQTT connect");
                return;
            }

            /* Reset retry counters */
            s_ctx.retry_count = 0;
            s_ctx.retry_delay_ms = s_ctx.config.min_retry_delay_ms;

            /* Build client config from stored config */
            mqtt_client_config_t client_cfg = {
                .broker_uri = s_ctx.config.broker_uri,
                .client_id = s_ctx.config.client_id[0] ? s_ctx.config.client_id : NULL,
                .username = s_ctx.config.username[0] ? s_ctx.config.username : NULL,
                .password = s_ctx.config.password[0] ? s_ctx.config.password : NULL,
                .keepalive = s_ctx.config.keepalive,
                .disable_clean_session = s_ctx.config.disable_clean_session,
                .lwt_qos = s_ctx.config.lwt_qos,
                .lwt_retain = s_ctx.config.lwt_retain,
                .lwt_topic = s_ctx.config.lwt_topic[0] ? s_ctx.config.lwt_topic : NULL,
                .lwt_message = s_ctx.config.lwt_message[0] ? s_ctx.config.lwt_message : NULL,
                .task_stack_size = 0,
                .task_priority = 0
            };

            /* Re-initialize client with new config (stop first if needed) */
            mqtt_client_stop();  /* Safe to call even if not started */
            esp_err_t err = mqtt_client_init(&client_cfg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init MQTT client: %s", esp_err_to_name(err));
                set_state(MQTT_STATE_FAILED);
                mqtt_event_connection_failed_t evt = { .attempts = s_ctx.retry_count + 1 };
                emit_event(EVENT_MQTT_CONNECTION_FAILED, &evt);
                return;
            }

            set_state(MQTT_STATE_CONNECTING);
            emit_event(EVENT_MQTT_CONNECTING, NULL);

            err = mqtt_client_start();
            if (err != ESP_OK) {
                /* Immediate failure -> go to FAILED */
                s_ctx.retry_count++;
                set_state(MQTT_STATE_FAILED);
                mqtt_event_connection_failed_t evt = { .attempts = s_ctx.retry_count };
                emit_event(EVENT_MQTT_CONNECTION_FAILED, &evt);
            }
            break;
        }

        case CMD_DISCONNECT_MQTT: {
            s_ctx.user_disconnect = true;

            if (s_ctx.state == MQTT_STATE_RECONNECT_WAIT) {
                stop_reconnect_timer();
                set_state(MQTT_STATE_IDLE);
            } else if (s_ctx.state == MQTT_STATE_CONNECTING ||
                       s_ctx.state == MQTT_STATE_CONNECTED) {
                mqtt_client_stop();
            } else if (s_ctx.state == MQTT_STATE_FAILED) {
                set_state(MQTT_STATE_IDLE);
            }
            break;
        }

        case CMD_SET_CONFIG_MQTT: {
            const cmd_set_config_mqtt_params_t *p = &cmd->data.set_config;
            strlcpy(s_ctx.config.broker_uri, p->broker_uri, sizeof(s_ctx.config.broker_uri));
            strlcpy(s_ctx.config.client_id, p->client_id, sizeof(s_ctx.config.client_id));
            strlcpy(s_ctx.config.username, p->username, sizeof(s_ctx.config.username));
            strlcpy(s_ctx.config.password, p->password, sizeof(s_ctx.config.password));
            s_ctx.config.keepalive = p->keepalive;
            s_ctx.config.disable_clean_session = p->disable_clean_session;
            s_ctx.config.lwt_qos = p->lwt_qos;
            s_ctx.config.lwt_retain = p->lwt_retain;
            strlcpy(s_ctx.config.lwt_topic, p->lwt_topic, sizeof(s_ctx.config.lwt_topic));
            strlcpy(s_ctx.config.lwt_message, p->lwt_message, sizeof(s_ctx.config.lwt_message));
            s_ctx.config.min_retry_delay_ms = p->min_retry_delay_ms;
            s_ctx.config.max_retry_delay_ms = p->max_retry_delay_ms;
            s_ctx.config.max_retry_attempts = p->max_retry_attempts;
            break;
        }

        case CMD_PUBLISH_MQTT: {
            if (s_ctx.state != MQTT_STATE_CONNECTED) {
                ESP_LOGW(TAG, "Publish ignored, not connected");
                return;
            }
            const cmd_publish_mqtt_params_t *p = &cmd->data.publish;
            mqtt_client_publish(p->topic, p->payload, p->payload_len, p->qos, p->retain);
            break;
        }

        case CMD_SUBSCRIBE_MQTT: {
            if (s_ctx.state != MQTT_STATE_CONNECTED) {
                ESP_LOGW(TAG, "Subscribe ignored, not connected");
                return;
            }
            const cmd_subscribe_mqtt_params_t *p = &cmd->data.subscribe;
            mqtt_client_subscribe(p->topic, p->qos);
            break;
        }

        case CMD_UNSUBSCRIBE_MQTT: {
            if (s_ctx.state != MQTT_STATE_CONNECTED) {
                ESP_LOGW(TAG, "Unsubscribe ignored, not connected");
                return;
            }
            const cmd_unsubscribe_mqtt_params_t *p = &cmd->data.unsubscribe;
            mqtt_client_unsubscribe(p->topic);
            break;
        }

        case CMD_MQTT_SET_WIFI_STATE:
            s_ctx.wifi_connected = (cmd->data.wifi_state != 0);
            ESP_LOGI(TAG, "WiFi state updated: %s", s_ctx.wifi_connected ? "connected" : "disconnected");
            break;

        default:
            ESP_LOGW(TAG, "Unknown command %d", cmd->cmd_id);
            break;
    }
}

/*============================================================================
 * Client Event Processing
 *============================================================================*/

static void process_client_event(const client_event_msg_t *evt)
{
    switch (evt->event) {
        case MQTT_CLIENT_EVENT_CONNECTED:
            if (s_ctx.state == MQTT_STATE_CONNECTING) {
                set_state(MQTT_STATE_CONNECTED);
                emit_event(EVENT_MQTT_CONNECTED, NULL);
                s_ctx.retry_count = 0;
                s_ctx.retry_delay_ms = s_ctx.config.min_retry_delay_ms;
            }
            break;

        case MQTT_CLIENT_EVENT_DISCONNECTED: {
            int reason = evt->data.error_code;
            mqtt_event_disconnected_t disc_evt = { .reason = reason };
            emit_event(EVENT_MQTT_DISCONNECTED, &disc_evt);

            if (s_ctx.user_disconnect) {
                s_ctx.user_disconnect = false;
                set_state(MQTT_STATE_IDLE);
                return;
            }

            /* Automatic reconnect logic (only if WiFi is connected) */
            if (s_ctx.wifi_connected &&
                (s_ctx.state == MQTT_STATE_CONNECTING ||
                 s_ctx.state == MQTT_STATE_CONNECTED ||
                 s_ctx.state == MQTT_STATE_RECONNECT_WAIT)) {

                s_ctx.retry_count++;

                if (s_ctx.config.max_retry_attempts > 0 &&
                    s_ctx.retry_count > s_ctx.config.max_retry_attempts) {
                    set_state(MQTT_STATE_FAILED);
                    mqtt_event_connection_failed_t fail_evt = { .attempts = s_ctx.retry_count };
                    emit_event(EVENT_MQTT_CONNECTION_FAILED, &fail_evt);
                    return;
                }

                start_reconnect_timer();
                set_state(MQTT_STATE_RECONNECT_WAIT);
            } else {
                /* WiFi not connected: stay in IDLE */
                set_state(MQTT_STATE_IDLE);
            }
            break;
        }

        case MQTT_CLIENT_EVENT_DATA:
            /* The message is already fully copied; just forward it */
            emit_event(EVENT_NETWORK_MESSAGE_RECEIVED, (void*)&evt->data.message);
            break;

        case MQTT_CLIENT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "Subscribed ack msg_id=%d", evt->data.msg_id);
            break;

        case MQTT_CLIENT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "Unsubscribed ack msg_id=%d", evt->data.msg_id);
            break;

        case MQTT_CLIENT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Published ack msg_id=%d", evt->data.msg_id);
            break;

        default:
            break;
    }
}

/*============================================================================
 * Reconnect Timer Processing
 *============================================================================*/

static void process_retry(void)
{
    if (s_ctx.state == MQTT_STATE_RECONNECT_WAIT && s_ctx.wifi_connected) {
        set_state(MQTT_STATE_CONNECTING);
        emit_event(EVENT_MQTT_CONNECTING, NULL);

        esp_err_t err = mqtt_client_start();
        if (err != ESP_OK) {
            /* Simulate disconnect to trigger retry logic */
            client_event_msg_t fake_evt;
            fake_evt.event = MQTT_CLIENT_EVENT_DISCONNECTED;
            fake_evt.data.error_code = -1;
            process_client_event(&fake_evt);
        }
    } else if (s_ctx.state == MQTT_STATE_RECONNECT_WAIT && !s_ctx.wifi_connected) {
        /* WiFi went down while waiting; go back to IDLE */
        stop_reconnect_timer();
        set_state(MQTT_STATE_IDLE);
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
 * Client Event Callback (called from MQTT client task)
 *============================================================================*/

static void client_event_callback(mqtt_client_event_t event, void *data)
{
    if (!s_ctx.queue) return;

    queue_item_t item;
    item.type = QUEUE_MSG_CLIENT_EVENT;
    item.msg.client_evt.event = event;

    switch (event) {
        case MQTT_CLIENT_EVENT_DISCONNECTED:
            if (data) item.msg.client_evt.data.error_code = *(int*)data;
            break;
        case MQTT_CLIENT_EVENT_DATA:
            if (data) {
                mqtt_client_data_t *src = (mqtt_client_data_t*)data;
                mqtt_message_t *msg = &item.msg.client_evt.data.message;
                strlcpy(msg->topic, src->topic, sizeof(msg->topic));
                size_t copy_len = src->payload_len;
                if (copy_len > sizeof(msg->payload)) copy_len = sizeof(msg->payload);
                memcpy(msg->payload, src->payload, copy_len);
                msg->payload_len = copy_len;
                msg->qos = src->qos;
                msg->retain = src->retain;
            }
            break;
        case MQTT_CLIENT_EVENT_SUBSCRIBED:
        case MQTT_CLIENT_EVENT_UNSUBSCRIBED:
        case MQTT_CLIENT_EVENT_PUBLISHED:
            if (data) item.msg.client_evt.data.msg_id = *(int*)data;
            break;
        default:
            break;
    }

    xQueueSend(s_ctx.queue, &item, 0);
}

/*============================================================================
 * State Management and Event Emission
 *============================================================================*/

static void set_state(mqtt_service_state_t new_state)
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
        case EVENT_MQTT_DISCONNECTED:
            if (data) {
                mqtt_event_disconnected_t *d = (mqtt_event_disconnected_t*)data;
                ev.data.mqtt_disconnected.reason = d->reason;
            }
            break;
        case EVENT_MQTT_CONNECTION_FAILED:
            if (data) {
                mqtt_event_connection_failed_t *f = (mqtt_event_connection_failed_t*)data;
                ev.data.mqtt_connection_failed.attempts = f->attempts;
            }
            break;
        case EVENT_NETWORK_MESSAGE_RECEIVED:
            if (data) {
                mqtt_message_t *m = (mqtt_message_t*)data;
                strlcpy(ev.data.mqtt_message.topic, m->topic, sizeof(ev.data.mqtt_message.topic));
                size_t copy_len = m->payload_len;
                if (copy_len > sizeof(ev.data.mqtt_message.payload)) copy_len = sizeof(ev.data.mqtt_message.payload);
                memcpy(ev.data.mqtt_message.payload, m->payload, copy_len);
                ev.data.mqtt_message.payload_len = copy_len;
                ev.data.mqtt_message.qos = m->qos;
                ev.data.mqtt_message.retain = m->retain;
            }
            break;
        default:
            /* No payload for CONNECTING, CONNECTED */
            break;
    }

    service_post_event(&ev);
}