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

/* ============================================================
 * Timer start commands – all share the same parameter struct
 * ============================================================ */
typedef struct {
    uint32_t timeout_ms;          /**< Timer duration in milliseconds */
} cmd_start_timer_params_t;

/* Servo command parameters */
typedef struct {
    servo_id_t servo_id;
    float angle_deg;          /* used by CMD_SERVO_SET_ANGLE, CMD_SERVO_OPEN/CLOSE also may use it */
} servo_command_data_t;

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
} command_param_union_t;

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_PARAMS_H */