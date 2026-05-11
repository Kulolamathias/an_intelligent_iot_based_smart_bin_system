/**
 * @file command_types.h
 * @brief System Command Definitions – Actions Only
 *
 * =============================================================================
 * ARCHITECTURAL DOCTRINE
 * =============================================================================
 * COMMANDS REPRESENT EXECUTABLE ACTIONS.
 * They do NOT encode policy, decisions, or transition logic.
 *
 * Commands are generated ONLY by the state_manager during transition evaluation.
 * They are routed by command_router and executed by registered service handlers.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: all possible command identifiers.
 * - Does NOT: execute, queue, or decide which commands to run.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - Command IDs are unique and stable.
 * - CMD_COUNT must always be the last enumerator.
 * - New commands must be added before CMD_COUNT.
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#ifndef COMMAND_TYPES_H
#define COMMAND_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * SYSTEM COMMAND IDENTIFIERS – ACTIONS ONLY
 *
 * Each ID corresponds to a distinct action that a service can perform.
 * Command execution is synchronous; services may not block indefinitely.
 * ============================================================ */
typedef enum {
    /* --------------------------------------------------------
     * No operation (reserved)
     * -------------------------------------------------------- */
    CMD_NONE = 0,

    /* --------------------------------------------------------
     * Actuation commands – physical output control --– domain level
     * -------------------------------------------------------- */
    CMD_OPEN_LID,                 /**< Open bin lid (servo) */
    CMD_CLOSE_LID,                  /**< Close bin lid (servo) */
    CMD_LOCK_BIN,                /**< Prevent lid opening */
    CMD_UNLOCK_BIN,                  /**< Allow lid opening */

    /* Servo – hardware level */
    CMD_SERVO_SET_ANGLE,
    CMD_SERVO_STOP,

    /* --------------------------------------------------------
     * Notification commands – external reporting
     * -------------------------------------------------------- */
    CMD_SEND_NOTIFICATION,       /**< Send status message (MQTT) */
    CMD_ESCALATE_NOTIFICATION,   /**< Send high-priority alert */
    CMD_SEND_HEARTBEAT,         /**< Send periodic presence signal */

    /* --------------------------------------------------------
     * Storage commands – non-volatile persistence
     * -------------------------------------------------------- */
    CMD_SAVE_SYSTEM_STATE,      /**< Persist current context */
    CMD_SAVE_CONFIGURATION,     /**< Persist runtime parameters */
    CMD_CLEAR_ERROR_LOG,        /**< Wipe error history */

    /* --------------------------------------------------------
     * User interface commands – visual/acoustic feedback
     * -------------------------------------------------------- */
    CMD_UPDATE_INDICATORS,      /**< Set LED patterns */
    CMD_PLAY_SOUND,             /**< Emit buzzer tone */

    /* --------------------------------------------------------
     * LCD commands
     * -------------------------------------------------------- */
    CMD_SHOW_MESSAGE,       /**< Display text on LCD (params: cmd_show_message_params_t) */
    CMD_CLEAR_LCD,          /**< Clear display and home cursor */
    CMD_SET_BACKLIGHT,      /**< Turn backlight on/off (params: cmd_backlight_params_t) */
    CMD_LCD_CURSOR,         /**< Set cursor position (params: cmd_cursor_params_t) */
    CMD_LCD_SCROLL_LEFT,    /**< Shift display left by one column */
    CMD_LCD_SCROLL_RIGHT,   /**< Shift display right by one column */  

    /* --------------------------------------------------------
     * Connectivity commands – network control
     * -------------------------------------------------------- */
    CMD_SYNC_NEIGHBORS,         /**< Request peer status exchange */
    CMD_SEND_STATUS_UPDATE,     /**< Transmit full status report */
    CMD_CONNECT_WIFI,           /**< Initiate WiFi connection */
    CMD_DISCONNECT_WIFI,        /**< Terminate WiFi link */
    CMD_SET_CONFIG_WIFI,        /**< Update WiFi parameters (params: cmd_set_config_wifi_params_t) */
    CMD_SCAN_WIFI,              /**< Perform WiFi scan, report results */
    CMD_GET_STATUS_WIFI,        /**< Query current WiFi status */

    CMD_CONNECT_MQTT,           /**< Initiate MQTT connection */
    CMD_DISCONNECT_MQTT,        /**< Terminate MQTT connection */
    CMD_SET_CONFIG_MQTT,        /**< Update MQTT parameters (params: cmd_set_config_mqtt_params_t) */
    CMD_PUBLISH_MQTT,           /**< Publish MQTT message (params: cmd_publish_mqtt_params_t) */
    CMD_SUBSCRIBE_MQTT,         /**< Subscribe to MQTT topic (params: cmd_subscribe_mqtt_params_t) */
    CMD_UNSUBSCRIBE_MQTT,       /**< Unsubscribe from MQTT topic (params: cmd_unsubscribe_mqtt_params_t) */
    CMD_MQTT_SET_WIFI_STATE,      /**< Inform MQTT service of WiFi connectivity (payload: bool connected) */

    CMD_CONNECT_GSM,            /**< Initiate GSM connection */
    CMD_DISCONNECT_GSM,         /**< Terminate GSM link */
    CMD_SEND_SMS,               /**< Send an SMS (params: cmd_send_sms_params_t) */
    CMD_SEND_SMS_RESPONSE,       /**< Send SMS reply (params: cmd_send_sms_response_params_t) */
    CMD_SET_GSM_PASSWORD,       /**< Set GSM password (params: string) – optional */
    CMD_SET_AUTHORIZED_NUMBERS, /**< Set authorized numbers (params: array) – optional */
    CMD_SET_COLLECTOR_NUMBER,
    CMD_SET_MANAGER_NUMBER,

    /* -----------------------------------------------------
     * GPS commands
     * ----------------------------------------------------- */
    CMD_GPS_START,              /**< Power on GPS and begin reading */
    CMD_GPS_STOP,               /**< Power off GPS */
    CMD_GPS_GET_LAST_FIX,       /**< Retrieve most recent GPS coordinates */
    // CMD_GPS_ADD_KNOWN_LOCATION, /**< Add/update a known location mapping */

    CMD_BIN_NET_NOTIFY_MQTT_CONNECTED,  /**< Inform neighbor bins of MQTT connection */
    CMD_BIN_NET_NOTIFY_NETWORK_MESSAGE, /**< Notify neighbor bins of network message */
    CMD_BIN_NET_NOTIFY_LEVEL_UPDATE,    /**< Notify neighbor bins of level update */

    CMD_PROCESS_WEB_COMMAND,        /**< Process incoming web command (params: web_command_params_t) */

    /* --------------------------------------------------------
     * Sensor commands – measurement triggers
     * -------------------------------------------------------- */
    CMD_READ_PRESENCE,          /**< Sample PIR/radar */
    CMD_READ_BATTERY_LEVEL,     /**< Query power supply */
    CMD_CALIBRATE_SENSORS,      /**< Perform sensor calibration */

    /* --------------------------------------------------------
     * Bin sensing
     * -------------------------------------------------------- */
    COMMAND_READ_FILL_LEVEL,        /**< Read fill level sensor (1st ultrasonic) TODO: COMMAND to CMD to change..*/
    COMMAND_READ_INTENT_SENSOR,     /**< Read intention sensor (tilted ultrasonic/radar)*/

    CMD_START_PIR_MONITORING,   /**< Start periodic PIR polling */
    CMD_STOP_PIR_MONITORING,    /**< Stop PIR polling */

    /* --------------------------------------------------------
     * LED commands
     * -------------------------------------------------------- */
    CMD_LED_ON,                 /**< Turn LED on (full brightness) */
    CMD_LED_OFF,                /**< Turn LED off */
    CMD_LED_TOGGLE,             /**< Toggle LED */
    CMD_LED_SET_BRIGHTNESS,     /**< Set LED brightness (params: led_brightness_params_t) */
    CMD_LED_BLINK,              /**< Start blinking (params: led_blink_params_t) */
    CMD_LED_BLINK_STOP,         /**< Stop blinking */
    CMD_LED_FADE,               /**< Smooth fade (params: led_fade_params_t) */

    /* --------------------------------------------------------
     * Buzzer commands
     * -------------------------------------------------------- */
    CMD_BUZZER_ON,              /**< Start continuous sound (params: buzzer_on_params_t) */
    CMD_BUZZER_OFF,             /**< Stop sound (params: buzzer_off_params_t) */
    CMD_BUZZER_BEEP,            /**< Single beep (params: buzzer_beep_params_t) */
    CMD_BUZZER_PATTERN,         /**< Play a predefined pattern (params: buzzer_pattern_params_t) */
    CMD_BUZZER_STOP,            /**< Stop any ongoing pattern */

    /* --------------------------------------------------------
     * Timer commands – software timer management
     * -------------------------------------------------------- */
    CMD_START_INTENT_TIMER,             /**< Begin intent window countdown */
    CMD_STOP_INTENT_TIMER,              /**< Cancel intent timer */
    CMD_START_ESCALATION_TIMER,         /**< Begin escalation delay */
    CMD_STOP_ESCALATION_TIMER,          /**< Cancel escalation timer */
    CMD_START_PERIODIC_TIMER,           /**< Begin periodic heartbeat cycle */
    CMD_STOP_PERIODIC_TIMER,            /**< Stop periodic heartbeat */
    CMD_START_ONESHOT_TIMER,            /**< Start a one‑shot timer (generic) */
    CMD_STOP_ONESHOT_TIMER,             /**< Stop a one‑shot timer (generic) */

    CMD_START_ONESHOT_TIMER_EX,         /**< Start oneshot timer with custom event (params: cmd_start_timer_ex_params_t) */

    /* --------------------------------------------------------
     * MQTT topic management commands
     * -------------------------------------------------------- */

    /* --------------------------------------------------------
     * Maintenance commands – service mode control
     * -------------------------------------------------------- */
    CMD_ENTER_MAINTENANCE_MODE,         /**< Switch to maintenance state */
    CMD_EXIT_MAINTENANCE_MODE,          /**< Leave maintenance state */
    CMD_RESET_STATISTICS,               /**< Clear operational counters */

    /* --------------------------------------------------------
     * Power management commands
     * -------------------------------------------------------- */
    CMD_ENTER_LOW_POWER,        /**< Reduce system activity */
    CMD_EXIT_LOW_POWER,         /**< Resume normal operation */

    /* --------------------------------------------------------
     * System commands – core control
     * -------------------------------------------------------- */
    CMD_REBOOT_SYSTEM,          /**< Restart entire system */
    CMD_FACTORY_RESET,          /**< Erase configuration, restore defaults */
    CMD_SIGNAL_ERROR,           /**< Indicate fault state (LED/buzzer) */
    CMD_CLEAR_ERROR,            /**< Clear error indication */

    /* --------------------------------------------------------
     * Sentinel – must always be the last element
     * -------------------------------------------------------- */
    CMD_COUNT
} system_command_id_t;

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_TYPES_H */