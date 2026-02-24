/**
 * @file event_types.h
 * @brief System Event Definitions – Facts Only
 *
 * =============================================================================
 * ARCHITECTURAL DOCTRINE
 * =============================================================================
 * EVENTS REPRESENT OBSERVABLE FACTS.
 * They do NOT encode decisions, interpretations, or policy.
 *
 * NAMING RULE:
 *   Every event name must complete the sentence:
 *   "The system observed that..."
 *
 * Example: EVENT_PERSON_DETECTED -> "The system observed that a person was detected."
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: all possible event identifiers and payload structures.
 * - Does NOT: generate, process, or decide based on events.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - Event IDs are unique and never reused.
 * - Payload union covers all event-specific data; unused fields are zero.
 *
 * =============================================================================
 * @version 2.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_netif.h"       // for esp_netif_ip_info_t
#include "core_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * SYSTEM EVENT IDENTIFIERS – FACTS ONLY
 *
 * Each ID corresponds to a distinct observable occurrence.
 * No decision or conclusion is implied by the existence of an event.
 * ============================================================ */
typedef enum {
    /* --------------------------------------------------------
     * Sensor observations (raw data from drivers)
     * -------------------------------------------------------- */
    EVENT_PERSON_DETECTED = 0,        /**< Person presence detected (by PIR), NOTYET intention evaluation */
    EVENT_PERSON_LEFT,                /**< Person no longer present */
    EVENT_CLOSE_RANGE_DETECTED,       /**< HERE: by tilt ultrasonic sensor just for intent */
    EVENT_CLOSE_RANGE_LOST,           /**< Here: close range no longer detected */
    EVENT_INTENT_SENSOR_READ,         /**<  */
    EVENT_INTENT_SIGNAL_DETECTED,     /**< Intent sensor (radar/ultrasonic) triggered */
    EVENT_INTENT_TIMEOUT,             /**< Intent evaluation period expired */
    EVENT_BIN_LEVEL_UPDATED,          /**< New fill level measurement available */

    /* --------------------------------------------------------
     * Timer events (system timebase)
     * -------------------------------------------------------- */
    EVENT_PERIODIC_REPORT_TIMEOUT,    /**< Time to send periodic status */
    EVENT_ONESHOT_REPORT_TIMEOUT,            /**< One‑shot timer expired (generic) */
    EVENT_ESCALATION_TIMEOUT,         /**< Escalation delay expired */

    /* --------------------------------------------------------
     * Connectivity events (network state changes)
     * -------------------------------------------------------- */
    EVENT_WIFI_CONNECTING,           /**< WiFi connection attempt initiated */
    EVENT_WIFI_CONNECTED,            /**< WiFi link established */
    EVENT_WIFI_GOT_IP,               /**< WiFi station obtained IP address */
    EVENT_WIFI_STATUS,               /**< WiFi status update (payload: wifi_service_state_t) */
    EVENT_WIFI_DISCONNECTED,         /**< WiFi link lost */
    EVENT_WIFI_CONNECTION_FAILED,    /**< WiFi connection attempt failed */
    EVENT_NETWORK_MESSAGE_RECEIVED,  /**< Incoming MQTT/network message */
    EVENT_MQTT_CONNECTING,           /**< MQTT connection attempt initiated */
    EVENT_MQTT_CONNECTED,            /**< MQTT connection established */
    EVENT_MQTT_DISCONNECTED,         /**< MQTT connection lost */
    EVENT_MQTT_CONNECTION_FAILED,    /**< MQTT connection attempt failed */
    EVENT_GSM_CONNECTED,             /**< GSM network registered */
    EVENT_GSM_DISCONNECTED,          /**< GSM network lost */
    EVENT_GSM_SMS_RECEIVED,          /**< SMS received (payload: sender, message) */
    EVENT_GSM_SMS_SENT,              /**< SMS sent successfully */
    EVENT_GSM_SMS_FAILED,            /**< SMS sending failed */
    EVENT_GSM_READY,                 /**< GSM module ready after init */
    EVENT_GSM_COMMAND_RECEIVED,      /**< Authenticated SMS command received (payload: sender, command) */

    /* --------------------------------------------------------
     * Authentication & maintenance
     * -------------------------------------------------------- */
    EVENT_AUTH_GRANTED,              /**< SMS authentication succeeded */
    EVENT_AUTH_DENIED,               /**< SMS authentication failed */

    /* --------------------------------------------------------
     * Intent validation results
     * -------------------------------------------------------- */
    EVENT_INTENT_VALIDATED,          /**< Intent pattern confirmed */
    EVENT_INTENT_REJECTED,           /**< Intent pattern invalid */

    /* --------------------------------------------------------
     * Configuration events
     * -------------------------------------------------------- */
    EVENT_CONFIG_UPDATED,            /**< Runtime configuration changed */
    EVENT_CONFIG_RESTORED,           /**< Configuration reloaded from NVS */

    /* --------------------------------------------------------
     * Actuation feedback (mechanical completion)
     * -------------------------------------------------------- */
    EVENT_LID_OPENED,               /**< Lid reached open position */
    EVENT_LID_CLOSED,               /**< Lid reached closed position */

    /* Servo events – hardware level only */
    EVENT_SERVO_MOVEMENT_STARTED,
    EVENT_SERVO_MOVEMENT_COMPLETED,
    EVENT_SERVO_ERROR,

    /* --------------------------------------------------------
     * System-level events
     * -------------------------------------------------------- */
    EVENT_SYSTEM_RESTARTED,         /**< System reboot completed */
    EVENT_SYSTEM_ERROR_DETECTED,    /**< Critical fault observed */

    /* --------------------------------------------------------
     * Bin-to-bin communication
     * -------------------------------------------------------- */
    EVENT_NEIGHBOR_STATUS_RECEIVED, /**< Status message from peer bin */

    /* --------------------------------------------------------
     * GPS / location
     * -------------------------------------------------------- */
    EVENT_GPS_COORDINATES_UPDATED,  /**< New GPS fix acquired */

    /* --------------------------------------------------------
     * Power management
     * -------------------------------------------------------- */
    EVENT_LOW_BATTERY_DETECTED,     /**< Battery below threshold */
    EVENT_BATTERY_NORMAL            /**< Battery recovered */

} system_event_id_t;


/* ============================================================
 * EVENT PAYLOAD UNION
 *
 * Each event type may carry associated observation data.
 * Payload is zero-initialized before posting; unused fields are ignored.
 * ============================================================ */

 // Payload structures for WiFi events
typedef struct {
    int reason;               /**< WiFi disconnection reason code */
} wifi_event_disconnected_t;

typedef struct {
    uint32_t attempts;        /**< Number of attempts made before failure */
} wifi_event_connection_failed_t;

typedef struct {
    int reason;
} mqtt_event_disconnected_t;

typedef struct {
    uint32_t attempts;
} mqtt_event_connection_failed_t;

typedef struct {
    char topic[128];
    uint8_t payload[256];
    size_t payload_len;
    int qos;
    bool retain;
} mqtt_message_t;


typedef struct {
    system_event_id_t id;           /**< Event type – MUST be first field */
    union {
        struct {
            uint8_t fill_level_percent; /**< 0-100% fill level */
        } bin_level;

        struct {
            uint8_t sender_mac[6];      /**< MAC address of peer bin */
            uint8_t fill_level_percent; /**< Peer's fill level */
            gps_coordinates_t location; /**< Peer's GPS coordinates */
        } neighbor_status;

        struct {
            char sender[16];
            char command[161];
        } gsm_command;

        struct {
            char sender[16];
            char message[161];
        } gsm_sms;

        struct {
            gps_coordinates_t coordinates; /**< Latitude, longitude, altitude */
        } gps_update;

        struct {
            uint8_t error_code;          /**< System-specific error code */
        } system_error;

        /* Servo event payload */
        struct {
            servo_id_t servo_id;
            float final_angle;
        } servo_event;

        struct {
            uint16_t distance_cm;   /**< Measured distance in centimeters */
        } intent_sensor;

        wifi_event_disconnected_t wifi_disconnected;
        wifi_event_connection_failed_t wifi_connection_failed;
        esp_netif_ip_info_t ip_info;         /**< for wifi got ip */
        uint32_t status_value;               /**< for wifi status */

        mqtt_event_disconnected_t mqtt_disconnected;
        mqtt_event_connection_failed_t mqtt_connection_failed;
        mqtt_message_t mqtt_message;

        /* Reserved for future expansion – always zero */
        struct {
            uint32_t raw;
        } reserved;
    } data;                             /**< Event-specific data */
} system_event_t;

#ifdef __cplusplus
}
#endif

#endif /* EVENT_TYPES_H */