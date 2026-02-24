#ifndef WIFI_PRIVATE_H
#define WIFI_PRIVATE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "wifi_driver_abstraction.h"
#include "service_interfaces.h"   // for service_post_event and event types
#include "command_params.h"   // for cmd_connect_wifi_params_t, cmd_set_config_wifi_params_t

#ifdef __cplusplus
extern "C" {
#endif

/* Queue message types */
typedef enum {
    QUEUE_MSG_COMMAND,
    QUEUE_MSG_DRIVER_EVENT,
    QUEUE_MSG_RETRY
} queue_msg_type_t;

/* Driver event message (copy of driver event with data) */
typedef struct {
    wifi_driver_event_t event;
    union {
        int disconnect_reason;
        esp_netif_ip_info_t ip_info;
    } data;
} driver_event_msg_t;

/* Command message (copy of command payload) */
typedef struct {
    system_command_id_t cmd_id;
    union {
        cmd_connect_wifi_params_t connect;
        cmd_set_config_wifi_params_t set_config;
        /* get_status has no data */
    } data;
} command_msg_t;

/* Main queue item */
typedef struct {
    queue_msg_type_t type;
    union {
        command_msg_t cmd;
        driver_event_msg_t drv_evt;
        /* retry message has no extra data */
    } msg;
} queue_item_t;

/* WiFi service states (internal) */
typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_RECONNECT_WAIT,
    WIFI_STATE_FAILED
} wifi_service_state_t;

/* WiFi service configuration (internal) */
typedef struct {
    uint32_t min_retry_delay_ms;
    uint32_t max_retry_delay_ms;
    uint32_t max_retry_attempts;   /* 0 = infinite retries */
} wifi_service_config_t;

/* WiFi service context (static inside wifi_service.c) */
struct wifi_service_context {
    TaskHandle_t task;
    QueueHandle_t queue;
    esp_timer_handle_t retry_timer;
    wifi_service_state_t state;
    wifi_service_config_t config;
    uint32_t retry_count;
    uint32_t retry_delay_ms;
    char current_ssid[32];
    char current_password[64];
    bool user_disconnect;
};

#ifdef __cplusplus
}
#endif

#endif /* WIFI_PRIVATE_H */