#ifndef MQTT_PRIVATE_H
#define MQTT_PRIVATE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "service_interfaces.h"
#include "mqtt_client_abstraction.h"
#include "command_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Queue message types */
typedef enum {
    QUEUE_MSG_COMMAND,
    QUEUE_MSG_CLIENT_EVENT,
    QUEUE_MSG_RETRY,
    QUEUE_MSG_WIFI_EVENT
} queue_msg_type_t;

/* Client event message (copy of client event with data) */
typedef struct {
    mqtt_client_event_t event;
    union {
        int msg_id;                     /* For SUBSCRIBED, UNSUBSCRIBED, PUBLISHED */
        mqtt_client_data_t data;        /* For DATA – note: topic/payload pointers are valid only during callback */
        int error_code;                 /* For DISCONNECTED */
    } data;
} client_event_msg_t;

/* Command message (copy of command payload) */
typedef struct {
    system_command_id_t cmd_id;
    union {
        cmd_connect_mqtt_params_t connect;
        cmd_set_config_mqtt_params_t set_config;
        cmd_publish_mqtt_params_t publish;
        cmd_subscribe_mqtt_params_t subscribe;
        cmd_unsubscribe_mqtt_params_t unsubscribe;
        uint32_t wifi_state;
        /* disconnect has no data */
    } data;
} command_msg_t;

/* Main queue item */
typedef struct {
    queue_msg_type_t type;
    union {
        command_msg_t cmd;
        client_event_msg_t client_evt;
        /* retry has no data */
    } msg;
} queue_item_t;

/* MQTT service states */
typedef enum {
    MQTT_STATE_IDLE,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_RECONNECT_WAIT,
    MQTT_STATE_FAILED
} mqtt_service_state_t;

/* MQTT service configuration */
typedef struct {
    char broker_uri[128];
    char client_id[64];
    char username[32];
    char password[32];
    int keepalive;
    bool disable_clean_session;
    int lwt_qos;
    bool lwt_retain;
    char lwt_topic[64];
    char lwt_message[64];
    uint32_t min_retry_delay_ms;
    uint32_t max_retry_delay_ms;
    uint32_t max_retry_attempts;   /* 0 = infinite */
} mqtt_service_config_t;

/* MQTT service context (static) */
struct mqtt_service_context {
    TaskHandle_t task;
    QueueHandle_t queue;
    esp_timer_handle_t retry_timer;
    mqtt_service_state_t state;
    mqtt_service_config_t config;
    uint32_t retry_count;
    uint32_t retry_delay_ms;
    bool user_disconnect;
    bool wifi_connected;            /* Track WiFi state for pause/resume */
};

#ifdef __cplusplus
}
#endif

#endif /* MQTT_PRIVATE_H */