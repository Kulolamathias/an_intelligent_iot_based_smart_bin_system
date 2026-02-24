#include "mqtt_client_abstraction.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "mqtt_client_abs";

/* Internal context (static, private) */
typedef struct {
    esp_mqtt_client_handle_t client;
    mqtt_client_event_cb_t user_cb;
    mqtt_client_config_t config;
    bool initialized;
    bool started;
} mqtt_client_context_t;

static mqtt_client_context_t s_ctx = {0};

/* Forward declarations */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/* Helper to copy string into newly allocated memory (duplicate) */
static char* copy_string(const char* src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char* dst = malloc(len);
    if (dst) {
        memcpy(dst, src, len);
    }
    return dst;
}

/* Free configuration strings */
static void free_config_strings(mqtt_client_config_t* cfg) {
    if (cfg->broker_uri)   { free((void*)cfg->broker_uri);   cfg->broker_uri = NULL; }
    if (cfg->client_id)    { free((void*)cfg->client_id);    cfg->client_id = NULL; }
    if (cfg->username)     { free((void*)cfg->username);     cfg->username = NULL; }
    if (cfg->password)     { free((void*)cfg->password);     cfg->password = NULL; }
    if (cfg->lwt_topic)    { free((void*)cfg->lwt_topic);    cfg->lwt_topic = NULL; }
    if (cfg->lwt_message)  { free((void*)cfg->lwt_message);  cfg->lwt_message = NULL; }
}

/*============================================================================
 * Public API
 *============================================================================*/

esp_err_t mqtt_client_init(const mqtt_client_config_t* config)
{
    if (s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Use defaults if config is NULL */
    mqtt_client_config_t default_config = {
        .broker_uri = "mqtt://test.mosquitto.org:1883",
        .client_id = NULL,
        .username = NULL,
        .password = NULL,
        .keepalive = 120,
        .disable_clean_session = false,
        .lwt_qos = 0,
        .lwt_retain = false,
        .lwt_topic = NULL,
        .lwt_message = NULL,
        .task_stack_size = 0,
        .task_priority = 0
    };

    const mqtt_client_config_t* src = config ? config : &default_config;

    /* Copy configuration strings */
    s_ctx.config.broker_uri   = copy_string(src->broker_uri);
    s_ctx.config.client_id    = copy_string(src->client_id);
    s_ctx.config.username     = copy_string(src->username);
    s_ctx.config.password     = copy_string(src->password);
    s_ctx.config.lwt_topic    = copy_string(src->lwt_topic);
    s_ctx.config.lwt_message  = copy_string(src->lwt_message);

    if (!s_ctx.config.broker_uri) {
        free_config_strings(&s_ctx.config);
        return ESP_ERR_NO_MEM;
    }

    s_ctx.config.keepalive             = src->keepalive;
    s_ctx.config.disable_clean_session = src->disable_clean_session;
    s_ctx.config.lwt_qos               = src->lwt_qos;
    s_ctx.config.lwt_retain            = src->lwt_retain;
    s_ctx.config.task_stack_size       = src->task_stack_size;
    s_ctx.config.task_priority         = src->task_priority;

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "MQTT client initialized");
    return ESP_OK;
}

esp_err_t mqtt_client_start(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctx.started) {
        return ESP_OK;  /* Idempotent */
    }

    /* Build esp-mqtt configuration */
    esp_mqtt_client_config_t esp_cfg = {
        .broker.address.uri = s_ctx.config.broker_uri,
        .credentials.client_id = s_ctx.config.client_id,
        .credentials.username = s_ctx.config.username,
        .credentials.authentication.password = s_ctx.config.password,
        .session.keepalive = s_ctx.config.keepalive,
        .session.disable_clean_session = s_ctx.config.disable_clean_session,
        .session.last_will.qos = s_ctx.config.lwt_qos,
        .session.last_will.retain = s_ctx.config.lwt_retain,
        .session.last_will.topic = s_ctx.config.lwt_topic,
        .session.last_will.msg = s_ctx.config.lwt_message,
        .task.stack_size = s_ctx.config.task_stack_size,
        .task.priority = s_ctx.config.task_priority,
    };

    s_ctx.client = esp_mqtt_client_init(&esp_cfg);
    if (!s_ctx.client) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    /* Register event handler */
    esp_err_t err = esp_mqtt_client_register_event(s_ctx.client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_ctx.client);
        s_ctx.client = NULL;
        return err;
    }

    /* Start client (connects asynchronously) */
    err = esp_mqtt_client_start(s_ctx.client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_ctx.client);
        s_ctx.client = NULL;
        return err;
    }

    s_ctx.started = true;
    ESP_LOGI(TAG, "MQTT client started");
    return ESP_OK;
}

esp_err_t mqtt_client_stop(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.started) {
        esp_err_t err = esp_mqtt_client_stop(s_ctx.client);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_mqtt_client_destroy(s_ctx.client);
        if (err != ESP_OK) {
            return err;
        }
        s_ctx.client = NULL;
        s_ctx.started = false;
    }

    /* Always free configuration strings and reset initialized */
    free_config_strings(&s_ctx.config);
    s_ctx.initialized = false;
    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

esp_err_t mqtt_client_publish(const char* topic,
                              const void* payload,
                              size_t len,
                              int qos,
                              bool retain)
{
    if (!s_ctx.initialized || !s_ctx.started || !s_ctx.client) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_publish(s_ctx.client, topic, (const char*)payload, len, qos, retain ? 1 : 0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mqtt_client_subscribe(const char* topic, int qos)
{
    if (!s_ctx.initialized || !s_ctx.started || !s_ctx.client) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_subscribe(s_ctx.client, topic, qos);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mqtt_client_unsubscribe(const char* topic)
{
    if (!s_ctx.initialized || !s_ctx.started || !s_ctx.client) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_unsubscribe(s_ctx.client, topic);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mqtt_client_register_callback(mqtt_client_event_cb_t cb)
{
    s_ctx.user_cb = cb;
    return ESP_OK;
}

/*============================================================================
 * Internal event handler (translates esp-mqtt events to our abstraction)
 *============================================================================*/

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    if (!s_ctx.user_cb) {
        return;
    }

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            s_ctx.user_cb(MQTT_CLIENT_EVENT_CONNECTED, NULL);
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_ctx.user_cb(MQTT_CLIENT_EVENT_DISCONNECTED, &event->error_handle->connect_return_code);
            break;

        case MQTT_EVENT_DATA: {
            mqtt_client_data_t data = {
                .topic = event->topic,
                .payload = event->data,
                .payload_len = event->data_len,
                .qos = event->qos,
                .retain = event->retain
            };
            s_ctx.user_cb(MQTT_CLIENT_EVENT_DATA, &data);
            break;
        }

        case MQTT_EVENT_SUBSCRIBED:
            s_ctx.user_cb(MQTT_CLIENT_EVENT_SUBSCRIBED, &event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            s_ctx.user_cb(MQTT_CLIENT_EVENT_UNSUBSCRIBED, &event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            s_ctx.user_cb(MQTT_CLIENT_EVENT_PUBLISHED, &event->msg_id);
            break;

        default:
            /* Ignore other events (e.g., before_connect, error) */
            break;
    }
}