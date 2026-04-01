/**
 * @file command_params.h
 * @brief Command Parameter Structures – Deterministic Data Passing
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module defines the parameter payloads for commands that require
 * additional data beyond the command ID.
 *
 * Parameter structures are:
 * - Allocated on the stack by state_manager during command batch execution.
 * - Filled by dedicated "preparer" functions.
 * - Passed as void* through command_router to service handlers.
 * - Cast back to the correct type by the receiving service.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: all command-specific parameter layouts.
 * - Does NOT: allocate, fill, or interpret parameter data.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - Every command that needs parameters MUST have a corresponding struct.
 * - All parameter structs are POD (plain old data) – no constructors, no pointers.
 * - The union command_param_union_t is sized to accommodate the largest struct.
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#ifndef COMMAND_PARAMS_H
#define COMMAND_PARAMS_H

#include <stdint.h>
#include <stdbool.h>
#include "core_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------
 * CMD_SEND_NOTIFICATION
 * ------------------------------------------------------------ */
typedef struct {
    char message[64];          /**< Notification text (null-terminated) */
    bool is_escalation;        /**< true = high priority, false = normal */
} cmd_send_notification_params_t;

/* CMD_SEND_SMS */
typedef struct {
    char phone_number[16];
    char message[161];
} cmd_send_sms_params_t;

/* */

/* ---------------------------------------------------------
 * GPS command parameters 
 * --------------------------------------------------------- */
typedef struct {
    double latitude;            /**< Latitude in degrees */
    double longitude;           /**< Longitude in degrees */
    uint16_t radius_meters;     /**< Matching radius (meters) */
    char name[64];              /**< Location name */
} gps_add_location_params_t;

typedef struct {
    char name[64];              /**< Name to assign to current location */
} gps_set_name_params_t;


/* ------------------------------------------------------------
 * CMD_SHOW_MESSAGE (LCD display)
 * ------------------------------------------------------------ */
typedef struct {
    char line1[20];            /**< First display line */
    char line2[20];            /**< Second display line */
} cmd_show_message_params_t;


/* ------------------------------------------------------------
 * CMD_UPDATE_INDICATORS (LEDs, buzzer)
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t led_pattern;       /**< Bitmask or pattern ID: 0=off, 1=solid, 2=slow blink, 3=fast blink, etc.*/
    uint8_t buzzer_pattern;    /**< Tone sequence ID */
    uint8_t lcd_pattern;       /**< LCD backlight blink pattern (same encoding) */
} cmd_update_indicators_params_t;


/* ------------------------------------------------------------
 * CMD_SEND_STATUS_UPDATE (MQTT report)
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t fill_level;               /**< 0-100% */
    gps_coordinates_t location;       /**< Current GPS fix */
    bool bin_locked;                 /**< Lock state */
} cmd_send_status_update_params_t;


/* ------------------------------------------------------------
 * Timer start commands – all share the same parameter struct
 * ------------------------------------------------------------ */
typedef struct {
    uint32_t timeout_ms;          /**< Timer duration in milliseconds */
} cmd_start_timer_params_t;


/* ------------------------------------------------------------
 * Servo commands (CMD_SERVO_SET_ANGLE, CMD_SERVO_OPEN_LID, CMD_SERVO_CLOSE_LID)
 * ------------------------------------------------------------ */
typedef struct {
    servo_id_t servo_id;
    float angle_deg;          /* used by CMD_SERVO_SET_ANGLE, CMD_SERVO_OPEN/CLOSE also may use it */
} servo_command_data_t;


/* ------------------------------------------------------------
 * WiFi command parameters
 * ------------------------------------------------------------ */
typedef struct {
    char ssid[32];
    char password[64];
    uint8_t auth_mode;  // from esp_wifi_types.h
} cmd_connect_wifi_params_t;

typedef struct {
    uint32_t min_retry_delay_ms;
    uint32_t max_retry_delay_ms;
    uint32_t max_retry_attempts;
} cmd_set_config_wifi_params_t;


/* ------------------------------------------------------------
 * MQTT command parameters
 * ------------------------------------------------------------ */
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
    uint32_t max_retry_attempts;
} cmd_connect_mqtt_params_t;

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
    uint32_t max_retry_attempts;
} cmd_set_config_mqtt_params_t;

typedef struct {
    char topic[128];
    char payload[256];
    size_t payload_len;
    int qos;
    bool retain;
} cmd_publish_mqtt_params_t;

typedef struct {
    char topic[128];
    int qos;
} cmd_subscribe_mqtt_params_t;

typedef struct {
    char topic[128];
} cmd_unsubscribe_mqtt_params_t;

typedef struct {
    char topic[128];
    uint8_t payload[512];
    size_t payload_len;
} cmd_bin_net_network_msg_t;

typedef struct {
    uint8_t fill_level_percent;
} cmd_bin_net_level_update_t;

typedef struct {
    char topic[128];                /**< MQTT topic */
    uint8_t payload[512];           /**< Command payload (JSON) */
    size_t payload_len;             /**< Payload length */
} web_command_params_t;

typedef struct {
    uint8_t sensor_id;  /* 0 = fill, 1 = intent */
} cmd_read_sensor_params_t;

/* For CMD_MQTT_SET_WIFI_STATE, we can use the generic status_value field.
 * No new struct needed, but we need a way to pass a boolean. The command
 * router passes a void*; we can cast a uint32_t. For clarity, define:
 */
typedef uint32_t cmd_wifi_state_t;  /* 0 = disconnected, 1 = connected */


/* ------------------------------------------------------------
 * LED command parameters
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t led_id;         /**< LED index (0..LED_COUNT-1) */
} led_id_params_t;

typedef struct {
    uint8_t led_id;         /**< LED index */
    uint8_t percent;        /**< Brightness 0‑100 */
} led_brightness_params_t;

typedef struct {
    uint8_t led_id;         /**< LED index */
    uint32_t period_ms;     /**< Blink period (ms) */
    uint8_t duty_percent;   /**< Duty cycle (0‑100) */
} led_blink_params_t;

typedef struct {
    uint8_t led_id;              /**< LED index */
    uint8_t target_percent;      /**< Target brightness */
    uint32_t duration_ms;        /**< Fade duration (ms) */
} led_fade_params_t;


/* ------------------------------------------------------------
 * Buzzer command parameters
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t buzzer_id;          /**< Buzzer instance ID (0..N-1) */
    uint32_t frequency_hz;      /**< Sound frequency in Hz */
    uint8_t duty_percent;       /**< Duty cycle (0‑100), usually 50 for square wave */
} buzzer_on_params_t;

typedef struct {
    uint8_t buzzer_id;          /**< Buzzer instance ID */
} buzzer_off_params_t;

typedef struct {
    uint8_t buzzer_id;          /**< Buzzer instance ID */
    uint32_t duration_ms;       /**< Beep duration in milliseconds */
    uint32_t frequency_hz;      /**< Sound frequency */
    uint8_t duty_percent;       /**< Duty cycle */
} buzzer_beep_params_t;

typedef struct {
    uint8_t buzzer_id;          /**< Buzzer instance ID */
    uint8_t pattern_id;         /**< Predefined pattern ID (0 = attention, 1 = success, 2 = error, etc.) */
} buzzer_pattern_params_t;


/* ============================================================
 * UNION OF ALL PARAMETER STRUCTURES
 *
 * This union provides a single stack buffer large enough for any
 * command parameter set. Zero-initialized before use.
 * ============================================================ */
typedef union {
    cmd_send_notification_params_t    send_notification;
    cmd_show_message_params_t         show_message;
    cmd_send_sms_params_t             send_sms;
    cmd_update_indicators_params_t    update_indicators;
    cmd_send_status_update_params_t   send_status_update;
    cmd_start_timer_params_t          start_timer;   /* for all timer start commands */
    servo_command_data_t              servo;
    cmd_connect_wifi_params_t         connect_wifi;
    cmd_set_config_wifi_params_t      set_config_wifi;
    cmd_connect_mqtt_params_t         connect_mqtt;
    cmd_set_config_mqtt_params_t      set_config_mqtt;
    cmd_publish_mqtt_params_t         publish_mqtt;
    cmd_subscribe_mqtt_params_t       subscribe_mqtt;
    cmd_unsubscribe_mqtt_params_t     unsubscribe_mqtt;
    cmd_read_sensor_params_t          read_sensor;
    web_command_params_t              web_cmd;
    led_id_params_t                   led_id;
    led_brightness_params_t           led_brightness;
    led_blink_params_t                led_blink;
    led_fade_params_t                 led_fade;
    buzzer_on_params_t                buzzer_on;
    buzzer_off_params_t               buzzer_off;
    buzzer_beep_params_t              buzzer_beep;
    buzzer_pattern_params_t           buzzer_pattern;
    gps_add_location_params_t         gps_add_location;
    gps_set_name_params_t             gps_set_name;
} command_param_union_t;

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_PARAMS_H */