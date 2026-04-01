/**
 * @file state_manager.c
 * @brief State Manager – Deterministic Transition Engine
 *
 * =============================================================================
 * ARCHITECTURAL DOCTRINE – READ CAREFULLY
 * =============================================================================
 * This file contains the COMPLETE BEHAVIORAL SPECIFICATION of the system.
 *
 * All possible system reactions are encoded in the static transition table.
 * The table is evaluated TOP TO BOTTOM; THE FIRST MATCHING RULE WINS.
 *
 * DECISION RULES:
 *   - Conditions must read ONLY from system_context_t.
 *   - Events are FACTS; they never directly influence decisions.
 *   - Command batches are executed strictly sequentially.
 *
 * EXTENSIBILITY MODEL:
 *   - New behavior is added by APPENDING new entries to g_transition_table.
 *   - Existing rules are NEVER modified (unless correcting a bug).
 *   - No dynamic rule registration – all decisions are compile-time.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * 1. system_state_get() is the sole source of current state.
 * 2. Context is updated BEFORE transition evaluation.
 * 3. No event payload data is used in conditions – only context.
 * 4. Every transition rule changes state OR remains in same state.
 * 5. Command parameters are allocated on stack and never persist.
 *
 * =============================================================================
 * @version 1.0.0
 * @author Core Architecture Group
 * =============================================================================
 */

#include "state_manager.h"
#include "command_router.h"
#include "command_params.h"
#include "system_state.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#define MAX_COMMANDS_PER_TRANSITION 12

/* Helper macro to define a command batch with automatic count */
#define COMMAND_BATCH(...) \
    { \
        .commands = { __VA_ARGS__ }, \
        .count = sizeof((transition_command_t[]){ __VA_ARGS__ }) / sizeof(transition_command_t) \
    }
    
static const char *TAG = "StateManager";

/* ============================================================
 * INTERNAL TYPE DEFINITIONS – TRANSITION RULE COMPONENTS
 * ============================================================ */

/**
 * @brief Function that prepares command parameters.
 * 
 * Called during command batch execution. Writes parameter data
 * into a stack-allocated union. Must be NULL if command needs no params.
 * 
 * @param ctx      Current system context (read-only)
 * @param event    Original event (may be NULL if not event-triggered)
 * @param params_out Pointer to command_param_union_t buffer
 */
typedef void (*param_preparer_t)(const system_context_t *ctx,
                                 const system_event_t *event,
                                 void *params_out);

/**
 * @brief A single command inside a transition batch.
 */
typedef struct {
    system_command_id_t cmd;            /**< Command to execute */
    param_preparer_t    prepare_params; /**< Parameter filler (may be NULL) */
} transition_command_t;

/**
 * @brief Batch of commands to execute after a transition.
 */
typedef struct {
    transition_command_t commands[MAX_COMMANDS_PER_TRANSITION];
    uint8_t count;                    /**< Number of valid commands */
} command_batch_t;

/**
 * @brief Condition function – must return true for rule to match.
 * 
 * IMPORTANT: Must read ONLY from system_context_t.
 * Event parameter is provided for completeness but MUST NOT be used.
 */
typedef bool (*transition_condition_t)(const system_context_t *ctx,
                                       const system_event_t *event);

/**
 * @brief Complete state transition rule.
 */
typedef struct {
    system_state_t          current_state;  /**< Required starting state */
    system_event_id_t       event_id;        /**< Required event */
    transition_condition_t  condition;       /**< Additional predicate (may be NULL) */
    system_state_t          next_state;      /**< State after transition */
    command_batch_t         command_batch;   /**< Actions to execute */
} state_transition_rule_t;

/* ============================================================
 * STATIC CONTEXT – SINGLE INSTANCE
 * ============================================================ */
static system_context_t g_context;


/* ============================================================
 * PARAMETER PREPARERS – COMMAND DATA PACKING
 *
 * These functions fill the parameter structures defined in command_params.h.
 * They are called only when a command that requires parameters is executed.
 * ============================================================ */

static void prepare_intent_timer(const system_context_t *ctx, const system_event_t *event, void *params_out)
{
    (void)ctx; (void)event;
    cmd_start_timer_params_t *p = params_out;
    p->timeout_ms = 5000;
}
static void prepare_escalation_timer(const system_context_t *ctx, const system_event_t *event, void *params_out)
{
    (void)ctx; (void)event;
    cmd_start_timer_params_t *p = params_out;
    p->timeout_ms = 3600000; /* 1 hour */
}
static void prepare_near_full_notification(const system_context_t *ctx, const system_event_t *event, void *params_out)
{
    (void)event;
    cmd_send_notification_params_t *p = params_out;
    snprintf(p->message, sizeof(p->message), "Bin near full: %d%%", ctx->bin_fill_level_percent);
    p->is_escalation = false;
}
static void prepare_full_notification(const system_context_t *ctx, const system_event_t *event, void *params_out)
{
    (void)event;
    cmd_send_notification_params_t *p = params_out;
    snprintf(p->message, sizeof(p->message), "Bin FULL at %d%%", ctx->bin_fill_level_percent);
    p->is_escalation = true;
}
static void prepare_empty_notification(const system_context_t *ctx, const system_event_t *event, void *params_out)
{
    (void)event;
    cmd_send_notification_params_t *p = params_out;
    snprintf(p->message, sizeof(p->message), "Bin emptied (now %d%%)", ctx->bin_fill_level_percent);
    p->is_escalation = false;
}
static void prepare_auth_success_response(const system_context_t *ctx, const system_event_t *event, void *params_out)
{
    (void)ctx; (void)event;
    cmd_send_sms_params_t *p = params_out;
    snprintf(p->phone_number, sizeof(p->phone_number), "%s", event->data.gsm_command.sender);
    snprintf(p->message, sizeof(p->message), "Authentication successful. Bin unlocked for maintenance.");
}
static void prepare_show_full_message(const system_context_t *ctx, const system_event_t *event, void *params_out)
{
    (void)ctx; (void)event;
    cmd_show_message_params_t *p = params_out;
    snprintf(p->line1, sizeof(p->line1), "BIN FULL");
    snprintf(p->line2, sizeof(p->line2), "Use another bin");
}

static void prepare_set_wifi_state(const system_context_t *ctx, const system_event_t *event, void *params_out)
{
    (void)ctx; (void)event;
    uint32_t *state = params_out;
    *state = 1;   // WiFi connected
}

/* ============================================================
 * LED PARAMETER PREPARERS
 * ============================================================ */

static void prepare_led_blink_attention(const system_context_t *ctx,
                                        const system_event_t *event,
                                        void *params_out)
{
    (void)ctx; (void)event;
    led_blink_params_t *p = params_out;
    p->led_id = 0;               /* white LED */
    p->period_ms = 500;
    p->duty_percent = 50;
}

static void prepare_led_blink_slow(const system_context_t *ctx,
                                   const system_event_t *event,
                                   void *params_out)
{
    (void)ctx; (void)event;
    led_blink_params_t *p = params_out;
    p->led_id = 2;               /* yellow LED */
    p->period_ms = 1000;
    p->duty_percent = 50;
}

static void prepare_led_blink_fast(const system_context_t *ctx,
                                   const system_event_t *event,
                                   void *params_out)
{
    (void)ctx; (void)event;
    led_blink_params_t *p = params_out;
    p->led_id = 3;               /* red LED */
    p->period_ms = 200;
    p->duty_percent = 50;
}

static void prepare_led_on_green(const system_context_t *ctx,
                                 const system_event_t *event,
                                 void *params_out)
{
    (void)ctx; (void)event;
    led_brightness_params_t *p = params_out;
    p->led_id = 1;               /* green LED */
    p->percent = 100;
}

static void prepare_led_on_blue(const system_context_t *ctx,
                                const system_event_t *event,
                                void *params_out)
{
    (void)ctx; (void)event;
    led_brightness_params_t *p = params_out;
    p->led_id = 4;               /* blue LED */
    p->percent = 100;
}

static void prepare_led_off_white(const system_context_t *ctx,
                                  const system_event_t *event,
                                  void *params_out)
{
    (void)ctx; (void)event;
    led_id_params_t *p = params_out;
    p->led_id = 0;
}

static void prepare_led_off_green(const system_context_t *ctx,
                                  const system_event_t *event,
                                  void *params_out)
{
    (void)ctx; (void)event;
    led_id_params_t *p = params_out;
    p->led_id = 1;
}

static void prepare_led_off_yellow(const system_context_t *ctx,
                                   const system_event_t *event,
                                   void *params_out)
{
    (void)ctx; (void)event;
    led_id_params_t *p = params_out;
    p->led_id = 2;
}

static void prepare_led_off_red(const system_context_t *ctx,
                                const system_event_t *event,
                                void *params_out)
{
    (void)ctx; (void)event;
    led_id_params_t *p = params_out;
    p->led_id = 3;
}

static void prepare_led_off_blue(const system_context_t *ctx,
                                 const system_event_t *event,
                                 void *params_out)
{
    (void)ctx; (void)event;
    led_id_params_t *p = params_out;
    p->led_id = 4;
}


/* ------------------------------------------------------------
 * BUZZER PARAMETER PREPARERS
 * ------------------------------------------------------------ */

/* on power-up/booting... */
static void prepare_buzzer_pattern_power_up(const system_context_t *ctx,
                                            const system_event_t *event,
                                            void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 12;   // power-up pattern
}

/* Person detected → friendly chirp (attention) */
static void prepare_buzzer_pattern_attention(const system_context_t *ctx,
                                             const system_event_t *event,
                                             void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 4;   /* friendly chirp */
}

/* Intention confirmed → success arpeggio */
static void prepare_buzzer_pattern_success(const system_context_t *ctx,
                                           const system_event_t *event,
                                           void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 5;   /* success arpeggio */
}

/* Lid closed → gentle fade */
static void prepare_buzzer_gentle_fade(const system_context_t *ctx,
                                       const system_event_t *event,
                                       void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 6;   /* gentle fade */
}

/* Intent timeout/person left → gentle fade (same) */
static void prepare_buzzer_gentle_fade_same(const system_context_t *ctx,
                                            const system_event_t *event,
                                            void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 6;
}

/* Near full → warning pulse */
static void prepare_buzzer_warning_pulse(const system_context_t *ctx,
                                         const system_event_t *event,
                                         void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 7;   /* warning pulse */
}

/* Full → urgent alarm */
static void prepare_buzzer_urgent_alarm(const system_context_t *ctx,
                                        const system_event_t *event,
                                        void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 8;   /* urgent alarm */
}

/* Escalation timeout → escalating siren */
static void prepare_buzzer_escalating_siren(const system_context_t *ctx,
                                            const system_event_t *event,
                                            void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 9;   /* escalating siren */
}

/* Maintenance granted → calming chord */
static void prepare_buzzer_calming_chord(const system_context_t *ctx,
                                         const system_event_t *event,
                                         void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 10;  /* calming chord */
}

/* Error → error glissando */
static void prepare_buzzer_error_glissando(const system_context_t *ctx,
                                           const system_event_t *event,
                                           void *params_out)
{
    (void)ctx; (void)event;
    buzzer_pattern_params_t *p = params_out;
    p->buzzer_id = 0;
    p->pattern_id = 11;  /* error glissando */
}

/* buzzer off*/
static void prepare_buzzer_off(const system_context_t *ctx,
                               const system_event_t *event,
                               void *params_out)
{
    (void)ctx; (void)event;
    buzzer_off_params_t *p = params_out;
    p->buzzer_id = 0;
}


/* ============================================================
 * TRANSITION CONDITION FUNCTIONS
 *
 * CRITICAL: These functions MUST NOT read from the event payload.
 * They MUST base their decision SOLELY on system_context_t.
 * ============================================================ */

/* Conditions */
static bool condition_bin_near_full(const system_context_t *ctx, const system_event_t *event)
{
    (void)event;
    return (ctx->bin_fill_level_percent >= ctx->params.near_full_threshold &&
            ctx->bin_fill_level_percent < ctx->params.full_threshold);
}
static bool condition_bin_full(const system_context_t *ctx, const system_event_t *event)
{
    (void)event;
    return (ctx->bin_fill_level_percent >= ctx->params.full_threshold);
}
static bool condition_bin_not_full(const system_context_t *ctx, const system_event_t *event)
{
    (void)event;
    return (ctx->bin_fill_level_percent < ctx->params.empty_threshold);
}
static bool condition_bin_not_near_full(const system_context_t *ctx, const system_event_t *event)
{
    (void)event;
    return (ctx->bin_fill_level_percent < ctx->params.near_full_threshold);
}


/* ============================================================
 * STATE TRANSITION TABLE – SINGLE SOURCE OF TRUTH
 *
 * EVALUATION RULES:
 *   1. Rules are evaluated in the order they appear in this table.
 *   2. The FIRST rule where (current_state, event_id, condition) matches is applied.
 *   3. No other rules are considered after a match.
 *   4. If no rule matches, the event is ignored (logged).
 *
 * This is intentional and deterministic.
 * ============================================================ */
static const state_transition_rule_t g_transition_table[] =
{
    /* --------------------------------------------------------
     * STATE: INIT → IDLE (when WiFi is connected)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_INIT,
        .event_id      = EVENT_WIFI_CONNECTED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_IDLE,
        .command_batch = COMMAND_BATCH(
            { CMD_START_PIR_MONITORING, NULL },  /* start PIR polling */
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_SEND_HEARTBEAT, NULL },
            { CMD_MQTT_SET_WIFI_STATE, prepare_set_wifi_state },
            { CMD_BUZZER_PATTERN, prepare_buzzer_pattern_power_up }
        )
    },

    /* --------------------------------------------------------
     * IDLE → ACTIVE (person detected by PIR)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_IDLE,
        .event_id      = EVENT_PERSON_DETECTED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_ACTIVE,
        .command_batch = COMMAND_BATCH(
            { CMD_UPDATE_INDICATORS, NULL },                /* LED/buzzer attention */
            { CMD_SHOW_MESSAGE, NULL },                     /* "Raise waste to open" */
            { CMD_LED_BLINK, prepare_led_blink_attention }, /* White LED (LED0) blink attention */
            { CMD_BUZZER_PATTERN, prepare_buzzer_pattern_attention }
        )
    },
    /* ----------------------------------------------------------------------------------------
     * IDLE → ACTIVE (intention confirmed(by event close range) – open lid, start timer)
     * --------------------------------------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_IDLE,
        .event_id      = EVENT_CLOSE_RANGE_DETECTED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_ACTIVE,
        .command_batch = COMMAND_BATCH(
            { CMD_OPEN_LID, NULL },
            { CMD_START_INTENT_TIMER, prepare_intent_timer },
            { CMD_UPDATE_INDICATORS, NULL },    /* success pattern */
            { CMD_LED_BLINK_STOP, NULL },   /* stop white LED blink (LED0) */
            { CMD_LED_ON, NULL },            /* turn green LED (LED1) on */
            { CMD_LED_BLINK, prepare_led_blink_attention },
            { CMD_BUZZER_PATTERN, prepare_buzzer_pattern_success }
        )
    },

    /* --------------------------------------------------------
     * ACTIVE → ACTIVE (intention confirmed – open lid, start timer)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_ACTIVE,
        .event_id      = EVENT_CLOSE_RANGE_DETECTED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_ACTIVE,      /* remain active */
        .command_batch = COMMAND_BATCH(
            { CMD_OPEN_LID, NULL },
            { CMD_START_INTENT_TIMER, prepare_intent_timer },
            { CMD_UPDATE_INDICATORS, NULL },
            { CMD_LED_BLINK_STOP, prepare_led_off_white },
            { CMD_LED_SET_BRIGHTNESS, prepare_led_on_green },
            { CMD_BUZZER_PATTERN, prepare_buzzer_pattern_success }
        )
    },

    /* --------------------------------------------------------
     * ACTIVE → IDLE (lid closed or timer expired, or person left)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_ACTIVE,
        .event_id      = EVENT_LID_CLOSED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_IDLE,
        .command_batch = COMMAND_BATCH(
            { CMD_STOP_INTENT_TIMER, NULL },
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_LED_OFF, prepare_led_off_white },
            { CMD_LED_OFF, prepare_led_off_green },
            { CMD_LED_OFF, prepare_led_off_yellow },
            { CMD_LED_OFF, prepare_led_off_red },
            { CMD_LED_OFF, prepare_led_off_blue },
            { CMD_LED_BLINK_STOP, prepare_led_off_white },
            { CMD_BUZZER_STOP, prepare_buzzer_gentle_fade }
        )
    },
    {
        .current_state = SYSTEM_STATE_ACTIVE,
        .event_id      = EVENT_INTENT_TIMEOUT,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_IDLE,
        .command_batch = COMMAND_BATCH(
                { CMD_CLOSE_LID, NULL },
                { CMD_UPDATE_DISPLAY, NULL },
                { CMD_LED_OFF, prepare_led_off_white },
                { CMD_LED_OFF, prepare_led_off_green },
                { CMD_LED_OFF, prepare_led_off_yellow },
                { CMD_LED_OFF, prepare_led_off_red },
                { CMD_LED_OFF, prepare_led_off_blue },
                { CMD_LED_BLINK_STOP, prepare_led_off_white },
                { CMD_BUZZER_STOP, prepare_buzzer_gentle_fade_same }
            )
    },
    {
        .current_state = SYSTEM_STATE_ACTIVE,
        .event_id      = EVENT_PERSON_LEFT,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_IDLE,
        .command_batch = COMMAND_BATCH(
            { CMD_CLOSE_LID, NULL },
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_LED_OFF, prepare_led_off_white },
            { CMD_LED_OFF, prepare_led_off_green },
            { CMD_LED_OFF, prepare_led_off_yellow },
            { CMD_LED_OFF, prepare_led_off_red },
            { CMD_LED_OFF, prepare_led_off_blue },
            { CMD_LED_BLINK_STOP, prepare_led_off_white },
            { CMD_BUZZER_STOP, prepare_buzzer_gentle_fade_same }
        )
    },

    /* --------------------------------------------------------
     * IDLE → NEAR_FULL (fill level reaches near-full threshold)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_IDLE,
        .event_id      = EVENT_FILL_LEVEL_UPDATED,
        .condition     = condition_bin_near_full,
        .next_state    = SYSTEM_STATE_NEAR_FULL,
        .command_batch = COMMAND_BATCH(
            { CMD_SEND_NOTIFICATION, prepare_near_full_notification },
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_LED_BLINK, prepare_led_blink_slow },
            { CMD_BUZZER_BEEP, prepare_buzzer_warning_pulse }
        )
    },

    /* --------------------------------------------------------
     * NEAR_FULL → FULL (fill level reaches full threshold)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_NEAR_FULL,
        .event_id      = EVENT_FILL_LEVEL_UPDATED,
        .condition     = condition_bin_full,
        .next_state    = SYSTEM_STATE_FULL,
        .command_batch = COMMAND_BATCH(
            { CMD_LOCK_BIN, NULL },
            { CMD_SEND_NOTIFICATION, prepare_full_notification },
            { CMD_SHOW_MESSAGE, prepare_show_full_message },
            { CMD_UPDATE_INDICATORS, NULL },
            { CMD_START_ESCALATION_TIMER, prepare_escalation_timer },
            { CMD_LED_BLINK_STOP, prepare_led_off_yellow },               /* stop yellow blink */
            { CMD_LED_BLINK, prepare_led_blink_fast },   /* start red fast blink */
            { CMD_BUZZER_PATTERN, prepare_buzzer_urgent_alarm }
        )
    },
    
    /* --------------------------------------------------------
     * NEAR_FULL → IDLE (fill level drops below near-full threshold)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_NEAR_FULL,
        .event_id      = EVENT_FILL_LEVEL_UPDATED,
        .condition     = condition_bin_not_near_full,
        .next_state    = SYSTEM_STATE_IDLE,
        .command_batch = COMMAND_BATCH(
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_LED_BLINK_STOP,   prepare_led_off_red },
            { CMD_LED_OFF,          prepare_led_off_red },
            { CMD_LED_OFF,          prepare_led_off_yellow },
            { CMD_LED_OFF,          prepare_led_off_white },
            { CMD_LED_OFF,          prepare_led_off_green },
            { CMD_LED_OFF,          prepare_led_off_blue },
            { CMD_BUZZER_STOP,      prepare_buzzer_off  }
        )
    },

    /* --------------------------------------------------------
     * FULL → IDLE (fill level drops below full threshold, e.g., after maintenance)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_FULL,
        .event_id      = EVENT_FILL_LEVEL_UPDATED,
        .condition     = condition_bin_not_full,
        .next_state    = SYSTEM_STATE_IDLE,
        .command_batch = COMMAND_BATCH(
            { CMD_UNLOCK_BIN, NULL },
            { CMD_SEND_NOTIFICATION, prepare_empty_notification },
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_STOP_ESCALATION_TIMER, NULL },
            { CMD_LED_BLINK_STOP,   prepare_led_off_red },
            { CMD_LED_OFF,          prepare_led_off_red },
            { CMD_LED_OFF,          prepare_led_off_yellow },
            { CMD_LED_OFF,          prepare_led_off_white },
            { CMD_LED_OFF,          prepare_led_off_green },
            { CMD_LED_OFF,          prepare_led_off_blue },
            { CMD_BUZZER_STOP,      prepare_buzzer_off  }
        )
    },

    /* --------------------------------------------------------
     * FULL → (stay) escalation timeout → escalate to manager
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_FULL,
        .event_id      = EVENT_ESCALATION_TIMEOUT,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_FULL,
        .command_batch = COMMAND_BATCH(
            { CMD_SEND_ESCALATION_NOTIFICATION, NULL },
            { CMD_BUZZER_PATTERN, prepare_buzzer_escalating_siren }
        )
    },

    /* --------------------------------------------------------
     * IDLE or FULL → MAINTENANCE (GSM authentication granted)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_IDLE,
        .event_id      = EVENT_AUTH_GRANTED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_MAINTENANCE,
        .command_batch = COMMAND_BATCH(
            { CMD_UNLOCK_BIN, NULL },
            { CMD_ENTER_MAINTENANCE_MODE, NULL },
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_SEND_SMS_RESPONSE, prepare_auth_success_response },
            { CMD_LED_BLINK_STOP, prepare_led_off_white },
            { CMD_LED_OFF, prepare_led_off_green },
            { CMD_LED_OFF, prepare_led_off_yellow },
            { CMD_LED_OFF, prepare_led_off_red },
            { CMD_LED_SET_BRIGHTNESS, prepare_led_on_blue },
            { CMD_BUZZER_PATTERN, prepare_buzzer_calming_chord }
        )
    },
    {
        .current_state = SYSTEM_STATE_FULL,
        .event_id      = EVENT_AUTH_GRANTED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_MAINTENANCE,
        .command_batch = COMMAND_BATCH(
            { CMD_UNLOCK_BIN, NULL },
            { CMD_ENTER_MAINTENANCE_MODE, NULL },
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_SEND_SMS_RESPONSE, prepare_auth_success_response },
            { CMD_LED_BLINK_STOP, prepare_led_off_white },
            { CMD_LED_OFF, prepare_led_off_green },
            { CMD_LED_OFF, prepare_led_off_yellow },
            { CMD_LED_OFF, prepare_led_off_red },
            { CMD_LED_SET_BRIGHTNESS, prepare_led_on_blue },
            { CMD_BUZZER_PATTERN, prepare_buzzer_calming_chord }
        )
    },

    /* --------------------------------------------------------
     * MAINTENANCE → IDLE (maintenance completed, e.g., via command)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_MAINTENANCE,
        .event_id      = EVENT_MAINTENANCE_COMPLETED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_IDLE,
        .command_batch = COMMAND_BATCH(
            { CMD_EXIT_MAINTENANCE_MODE, NULL },
            { CMD_SAVE_CONFIGURATION, NULL },
            { CMD_LOCK_BIN, NULL },
            { CMD_UPDATE_DISPLAY, NULL },
            { CMD_LED_OFF, prepare_led_off_blue }
        )
    },

    /* --------------------------------------------------------
     * ANY STATE → ERROR (sensor failure or critical fault)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_ANY,
        .event_id      = EVENT_SENSOR_FAILURE,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_ERROR,
        .command_batch = COMMAND_BATCH( 
            { CMD_SIGNAL_ERROR, NULL },
            { CMD_LED_BLINK_STOP, prepare_led_off_white },
            { CMD_LED_BLINK_STOP, prepare_led_off_green },
            { CMD_LED_BLINK_STOP, prepare_led_off_yellow },
            { CMD_LED_BLINK_STOP, prepare_led_off_red },
            { CMD_LED_BLINK_STOP, prepare_led_off_blue },
            { CMD_LED_BLINK, prepare_led_blink_fast },
            { CMD_BUZZER_PATTERN, prepare_buzzer_error_glissando }
        )
    },
    {
        .current_state = SYSTEM_STATE_ANY,
        .event_id      = EVENT_SYSTEM_ERROR_DETECTED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_ERROR,
        .command_batch = COMMAND_BATCH(
            { CMD_SIGNAL_ERROR, NULL },
            { CMD_LED_BLINK_STOP, prepare_led_off_white },
            { CMD_LED_BLINK_STOP, prepare_led_off_green },
            { CMD_LED_BLINK_STOP, prepare_led_off_yellow },
            { CMD_LED_BLINK_STOP, prepare_led_off_red },
            { CMD_LED_BLINK_STOP, prepare_led_off_blue },
            { CMD_LED_BLINK, prepare_led_blink_fast },
            { CMD_BUZZER_PATTERN, prepare_buzzer_error_glissando }
        )
    },

    /* --------------------------------------------------------
     * ANY STATE → SAME STATE 
     * -------------------------------------------------------- */

    /* ANY STATE → SAME STATE (update MQTT about WiFi connection) */
    {
        .current_state = SYSTEM_STATE_ANY,
        .event_id      = EVENT_WIFI_CONNECTED,
        .condition     = NULL,
        .next_state    = SYSTEM_STATE_ANY,
        .command_batch = COMMAND_BATCH(
            { CMD_MQTT_SET_WIFI_STATE, prepare_set_wifi_state }
        )
    },
};

#define TRANSITION_TABLE_SIZE \
    (sizeof(g_transition_table) / sizeof(g_transition_table[0]))



/* ============================================================
 * INTERNAL HELPER: EXECUTE COMMAND BATCH
 *
 * - Allocates a stack buffer large enough for any command parameters.
 * - For each command, if a preparer exists, calls it to fill the buffer.
 * - Passes the buffer pointer (or NULL) to command_router_execute().
 * - Does NOT modify context or state.
 * ============================================================ */
static void execute_command_batch(const command_batch_t *batch)
{
    command_param_union_t param_buffer;

    for (uint8_t i = 0; i < batch->count; i++) {
        const transition_command_t *tc = &batch->commands[i];
        void *params = NULL;

        if (tc->prepare_params != NULL) {
            memset(&param_buffer, 0, sizeof(param_buffer));
            tc->prepare_params(&g_context, NULL, &param_buffer);
            params = &param_buffer;
        }

        esp_err_t ret = command_router_execute(tc->cmd, params);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Command %d returned %d", tc->cmd, ret);
        }
    }
}

/* ============================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================ */

esp_err_t state_manager_init(const system_context_t *initial_context)
{
    if (initial_context == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    system_state_init();
    memcpy(&g_context, initial_context, sizeof(system_context_t));

    ESP_LOGI(TAG, "State manager initialized. Current state: %s",
             system_state_to_string(system_state_get()));
    return ESP_OK;
}

esp_err_t state_manager_process_event(const system_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* --------------------------------------------------------
     * 1. UPDATE CONTEXT – absorb observed facts
     * -------------------------------------------------------- */
    switch (event->id) {
        case EVENT_FILL_LEVEL_UPDATED:
            g_context.bin_fill_level_percent = event->data.fill_level.fill_percent;
            cmd_bin_net_level_update_t lvl = {
                .fill_level_percent = event->data.fill_level.fill_percent
            };
            command_router_execute(CMD_BIN_NET_NOTIFY_LEVEL_UPDATE, &lvl);
            break;
        case EVENT_AUTH_GRANTED:
            g_context.auth_status = AUTH_STATUS_GRANTED;
            break;
        case EVENT_AUTH_DENIED:
            g_context.auth_status = AUTH_STATUS_DENIED;
            break;
        case EVENT_GPS_COORDINATES_UPDATED:
            g_context.gps_coordinates = event->data.gps_update.coordinates;
            break;
        case EVENT_MQTT_CONNECTED:
            command_router_execute(CMD_BIN_NET_NOTIFY_MQTT_CONNECTED, NULL);
            break;

        case EVENT_NETWORK_MESSAGE_RECEIVED:
            cmd_bin_net_network_msg_t msg;
            strlcpy(msg.topic, event->data.mqtt_message.topic, sizeof(msg.topic));
            memcpy(msg.payload, event->data.mqtt_message.payload, event->data.mqtt_message.payload_len);
            msg.payload_len = event->data.mqtt_message.payload_len;
            command_router_execute(CMD_BIN_NET_NOTIFY_NETWORK_MESSAGE, &msg);
            break;
        /* EVENT_NEIGHBOR_STATUS_RECEIVED: core does not store neighbor data */
        default:
            break;
    }

    /* --------------------------------------------------------
     * Forward web command messages to the web command service
     * -------------------------------------------------------- */
    if (event->id == EVENT_NETWORK_MESSAGE_RECEIVED) {
        /**< For debugging: logging relevant event data when msg is recieved */
        ESP_LOGI(TAG, "MQTT received: \n\t topic=%.*s, \n\t payload=%.*s",
                (int)strlen(event->data.mqtt_message.topic), event->data.mqtt_message.topic,
                (int)event->data.mqtt_message.payload_len, event->data.mqtt_message.payload);

        // web_command_params_t params;
        // strlcpy(params.topic, event->data.mqtt_message.topic, sizeof(params.topic));
        // size_t copy_len = event->data.mqtt_message.payload_len;
        // if (copy_len > sizeof(params.payload)) copy_len = sizeof(params.payload);
        // memcpy(params.payload, event->data.mqtt_message.payload, copy_len);
        // params.payload_len = copy_len;
        // command_router_execute(CMD_PROCESS_WEB_COMMAND, &params);
    }

    /**< For debugging: logging GPS fix updates */
    if (event->id == EVENT_GPS_FIX_UPDATED) {
        ESP_LOGI(TAG, "GPS fix: lat=%.6f, lon=%.6f, alt=%.1f, sats=%u, hdop=%.1f",
                event->data.gps_fix.latitude,
                event->data.gps_fix.longitude,
                event->data.gps_fix.altitude_m,
                event->data.gps_fix.satellites,
                event->data.gps_fix.hdop);
    } else if (event->id == EVENT_GPS_FIX_LOST) {
        ESP_LOGI(TAG, "GPS fix lost");
    }

    /* --------------------------------------------------------
     * 2. EVALUATE TRANSITION TABLE – first match wins
     * -------------------------------------------------------- */
    system_state_t current_state = system_state_get();

    for (size_t i = 0; i < TRANSITION_TABLE_SIZE; i++) {
        const state_transition_rule_t *rule = &g_transition_table[i];

        if (rule->current_state != current_state) continue;
        if (rule->event_id != event->id) continue;
        if (rule->condition != NULL && !rule->condition(&g_context, event)) continue;

        ESP_LOGI(TAG, "Transition: %s -> %s (event %d)",
                 system_state_to_string(current_state),
                 system_state_to_string(rule->next_state),
                 event->id);

        system_state_set(rule->next_state);
        execute_command_batch(&rule->command_batch);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "No transition for event %d in state %s",
             event->id, system_state_to_string(current_state));
    return ESP_OK;
}

const system_context_t* state_manager_get_context(void)
{
    return &g_context;
}