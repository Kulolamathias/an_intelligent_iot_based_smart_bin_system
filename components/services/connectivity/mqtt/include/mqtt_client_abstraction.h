/**
 * @file mqtt_client_abstraction.h
 * @brief MQTT Client Abstraction – Transport‑only Wrapper for ESP‑MQTT
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This layer encapsulates the ESP‑IDF MQTT client driver. It provides a
 * simplified, deterministic interface for the higher‑level MQTT service.
 * No business logic, reconnect logic, or topic parsing is present.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: configuration structures, event types, and public API.
 * - Does NOT: maintain any persistent state outside its own static context.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - All public functions validate their arguments.
 * - The internal context is static and not exposed to callers.
 * - Strings passed in the configuration are copied internally; the caller
 *   may free them immediately after calling mqtt_client_init().
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#ifndef MQTT_CLIENT_ABSTRACTION_H
#define MQTT_CLIENT_ABSTRACTION_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT client configuration structure.
 *
 * All strings are expected to be null-terminated and remain valid
 * for the lifetime of the client (they are copied internally).
 */
typedef struct {
    const char* broker_uri;          /**< Broker URI, e.g., "mqtt://test.mosquitto.org:1883" */
    const char* client_id;            /**< Client identifier (optional, may be NULL) */
    const char* username;             /**< Username (optional, may be NULL) */
    const char* password;             /**< Password (optional, may be NULL) */
    int keepalive;                    /**< Keepalive in seconds (0 = use default) */
    bool disable_clean_session;       /**< If true, disable clean session (retain state) */
    int lwt_qos;                      /**< Last Will QoS (0,1,2) */
    bool lwt_retain;                  /**< Last Will retain flag */
    const char* lwt_topic;            /**< Last Will topic (optional) */
    const char* lwt_message;          /**< Last Will message (optional) */
    int task_stack_size;              /**< MQTT task stack size (0 = default) */
    int task_priority;                /**< MQTT task priority (0 = default) */
} mqtt_client_config_t;

/**
 * @brief MQTT client events.
 */
typedef enum {
    MQTT_CLIENT_EVENT_CONNECTED,      /**< Successfully connected to broker (data: NULL) */
    MQTT_CLIENT_EVENT_DISCONNECTED,   /**< Disconnected from broker (data: int* error code) */
    MQTT_CLIENT_EVENT_DATA,           /**< Incoming message (data: mqtt_client_data_t*) */
    MQTT_CLIENT_EVENT_SUBSCRIBED,     /**< Subscription acknowledged (data: int* msg_id) */
    MQTT_CLIENT_EVENT_UNSUBSCRIBED,   /**< Unsubscription acknowledged (data: int* msg_id) */
    MQTT_CLIENT_EVENT_PUBLISHED       /**< Publish acknowledged (data: int* msg_id) */
} mqtt_client_event_t;

/**
 * @brief Data structure for incoming MQTT messages.
 */
typedef struct {
    const char* topic;                 /**< Topic string (null-terminated, valid only during callback) */
    const void* payload;               /**< Payload data (valid only during callback) */
    size_t payload_len;                /**< Length of payload in bytes */
    int qos;                           /**< QoS of the message */
    bool retain;                       /**< Retain flag */
} mqtt_client_data_t;

/**
 * @brief Callback type for MQTT client events.
 *
 * @param event Event type
 * @param data  Event-specific data (see event descriptions). Valid only during callback.
 */
typedef void (*mqtt_client_event_cb_t)(mqtt_client_event_t event, void* data);

/**
 * @brief Initialize the MQTT client with the given configuration.
 *
 * This function must be called once before any other functions.
 * The configuration is copied internally; the caller can free the strings afterwards.
 *
 * @param config Pointer to configuration structure (may be NULL for defaults).
 * @return ESP_OK on success, or an error code.
 */
esp_err_t mqtt_client_init(const mqtt_client_config_t* config);

/**
 * @brief Start the MQTT client (connects to broker).
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t mqtt_client_start(void);

/**
 * @brief Stop the MQTT client (disconnects and frees resources).
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t mqtt_client_stop(void);

/**
 * @brief Publish a message to a topic.
 *
 * @param topic   Topic string (null-terminated).
 * @param payload Pointer to payload data.
 * @param len     Length of payload (bytes).
 * @param qos     Quality of Service (0,1,2).
 * @param retain  Retain flag.
 * @return ESP_OK on success (message queued), or an error code.
 */
esp_err_t mqtt_client_publish(const char* topic,
                              const void* payload,
                              size_t len,
                              int qos,
                              bool retain);

/**
 * @brief Subscribe to a topic.
 *
 * @param topic Topic string (null-terminated).
 * @param qos   Requested QoS.
 * @return ESP_OK on success (subscribe request queued), or an error code.
 */
esp_err_t mqtt_client_subscribe(const char* topic, int qos);

/**
 * @brief Unsubscribe from a topic.
 *
 * @param topic Topic string (null-terminated).
 * @return ESP_OK on success (unsubscribe request queued), or an error code.
 */
esp_err_t mqtt_client_unsubscribe(const char* topic);

/**
 * @brief Register a callback for MQTT client events.
 *
 * Only one callback can be registered. Calling this function again replaces
 * the previous callback. The callback is invoked from the MQTT task context
 * and must not block.
 *
 * @param cb Callback function, or NULL to unregister.
 * @return ESP_OK always.
 */
esp_err_t mqtt_client_register_callback(mqtt_client_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CLIENT_ABSTRACTION_H */