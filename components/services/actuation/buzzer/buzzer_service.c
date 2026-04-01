/**
 * @file components/services/actuation/buzzer_service/buzzer_service.c
 * @brief Buzzer Service – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Creates a fixed number of buzzer instances based on Kconfig or hardcoded pins.
 * - Uses a single `esp_timer` for timing beeps and pattern steps.
 * - Patterns are stored as arrays of (frequency, duration_ms) pairs.
 * - Only one pattern/beep can be active at a time; new commands stop the current.
 * - Multiple buzzer instances are supported via `buzzer_id` parameter.
 * =============================================================================
 */

#include "buzzer_service.h"
#include "buzzer_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>


/* ============================================================
 * Configuration – number of buzzers and their pins
 * ============================================================ */
#define BUZZER_COUNT 1                  /* Only one buzzer for now; can be extended */
#define BUZZER_GPIO_NUM GPIO_NUM_23     /* GPIO pin for the buzzer */

static const char *TAG = "BUZZER_SVC";


static const buzzer_config_t s_buzzer_configs[BUZZER_COUNT] = {
    {
        .gpio_num = BUZZER_GPIO_NUM,
        .channel = LEDC_CHANNEL_3,
        .timer = LEDC_TIMER_2,
        .default_freq_hz = 2000,
        .default_duty_percent = 50
    }
};

static buzzer_handle_t s_buzzers[BUZZER_COUNT] = {NULL};
static esp_timer_handle_t s_timer = NULL;

/* Current active playback */
typedef struct {
    uint8_t buzzer_id;
    bool active;
    bool is_pattern;
    uint32_t freq_hz;
    uint8_t duty;
    uint32_t duration_ms;
    /* For pattern playback */
    uint8_t pattern_idx;
    const uint16_t *pattern;   /* pointer to pattern data (freq, duration pairs) */
    uint32_t pattern_len;       /* number of elements (pairs count) */
} playback_ctx_t;

static playback_ctx_t s_playback = {0};

/* Predefined patterns (frequency in Hz, duration in ms) – stored as uint16_t */
/* Attention: 3 short beeps (200 ms on, 100 ms off) */
static const uint16_t s_pattern_attention[] = {
    2000, 200,   /* freq, duration */
    0,    100,   /* pause */
    2000, 200,
    0,    100,
    2000, 200,
    0,    0      /* sentinel */
};

/* Success: one short high beep */
static const uint16_t s_pattern_success[] = {
    3000, 300,
    0,    0
};

/* Error: long low beep */
static const uint16_t s_pattern_error[] = {
    1000, 1000,
    0,    0
};

/* Full: continuous fast beep (repeating) */
static const uint16_t s_pattern_full[] = {
    2000, 200,
    0,    100,
    2000, 200,
    0,    100,
    2000, 200,
    0,    100,
    0,    0
};

/* Pattern 4: Friendly chirp (rising, for attention) */
static const uint16_t s_pattern_friendly_chirp[] = {
    800, 100, 1000, 100, 1200, 100, 1500, 200,
    0,0
};

/* Pattern 5: Success arpeggio (C major chord: C-E-G-C) */
static const uint16_t s_pattern_success_arpeggio[] = {
    262, 150, 330, 150, 392, 150, 523, 300,
    0,0
};

/* Pattern 6: Gentle fading tone (for lid close / timeout) */
static const uint16_t s_pattern_gentle_fade[] = {
    800, 300, 600, 300, 400, 300, 200, 500,
    0,0
};

/* Pattern 7: Warning pulsation (slow heartbeat) */
static const uint16_t s_pattern_warning_pulse[] = {
    600, 200, 0, 200, 600, 200, 0, 200,
    600, 200, 0, 200, 600, 200, 0, 200,
    0,0
};

/* Pattern 8: Urgent alarm (repeating, accelerating) */
static const uint16_t s_pattern_urgent_alarm[] = {
    1000, 100, 0, 50, 1000, 100, 0, 40,
    1000, 100, 0, 30, 1000, 100, 0, 20,
    1000, 100, 0, 10, 1000, 200, 0, 0
};

/* Pattern 9: Escalating siren (alternating high-low, increasing tempo) */
static const uint16_t s_pattern_escalating_siren[] = {
    800, 200, 1200, 200, 800, 150, 1200, 150,
    800, 100, 1200, 100, 800, 50, 1200, 50,
    800, 50, 1200, 50, 800, 50, 1200, 50,
    0,0
};

/* Pattern 10: Calming chord (sustained major chord) */
static const uint16_t s_pattern_calming_chord[] = {
    262, 500, 330, 500, 392, 500, 523, 1000,
    0,0
};

/* Pattern 11: Error glissando (descending, harsh) */
static const uint16_t s_pattern_error_glissando[] = {
    2000, 50, 1900, 50, 1800, 50, 1700, 50,
    1600, 50, 1500, 50, 1400, 50, 1300, 50,
    1200, 50, 1100, 50, 1000, 50, 900, 50,
    800, 100, 700, 100, 600, 100, 500, 300,
    0,0
};

/* Pattern 12: Power-up / system ready (rising sweep + chord) */
static const uint16_t s_pattern_power_up[] = {
    400, 100, 600, 100, 800, 100, 1000, 100,
    1200, 100, 1400, 100, 1600, 100, 1800, 100,
    2000, 100, 1800, 100, 1600, 100, 1400, 100,
    1200, 100, 1000, 100, 800, 100, 600, 100,
    400, 300, 0,0
};

/* Helper to get buzzer handle */
static buzzer_handle_t get_buzzer(uint8_t buzzer_id)
{
    if (buzzer_id >= BUZZER_COUNT) return NULL;
    return s_buzzers[buzzer_id];
}

/* Timer callback for playback */
static void playback_timer_callback(void *arg)
{
    (void)arg;

    if (!s_playback.active) return;

    if (s_playback.is_pattern) {
        /* Pattern playback */
        const uint16_t *p = s_playback.pattern + s_playback.pattern_idx;
        uint32_t freq = p[0];
        uint32_t dur = p[1];
        if (freq == 0 && dur == 0) {
            /* End of pattern */
            buzzer_driver_stop(get_buzzer(s_playback.buzzer_id));
            s_playback.active = false;
            return;
        }

        if (freq == 0) {
            /* Pause: stop sound, restart timer with pause duration */
            buzzer_driver_stop(get_buzzer(s_playback.buzzer_id));
            esp_timer_start_once(s_timer, dur * 1000);
            s_playback.pattern_idx += 2;
        } else {
            /* Play tone */
            buzzer_driver_start_tone(get_buzzer(s_playback.buzzer_id), freq, s_playback.duty);
            esp_timer_start_once(s_timer, dur * 1000);
            s_playback.pattern_idx += 2;
        }
    } else {
        /* Single beep: after duration, stop */
        buzzer_driver_stop(get_buzzer(s_playback.buzzer_id));
        s_playback.active = false;
    }
}

/* ============================================================
 * Command handlers (same as before, but with corrected param types)
 * ============================================================ */

static esp_err_t cmd_buzzer_on(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const buzzer_on_params_t *p = &params->buzzer_on;
    buzzer_handle_t buzzer = get_buzzer(p->buzzer_id);
    if (!buzzer) return ESP_ERR_INVALID_ARG;

    if (s_playback.active) {
        esp_timer_stop(s_timer);
        s_playback.active = false;
    }

    return buzzer_driver_start_tone(buzzer, p->frequency_hz, p->duty_percent);
}

static esp_err_t cmd_buzzer_off(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const buzzer_off_params_t *p = &params->buzzer_off;
    buzzer_handle_t buzzer = get_buzzer(p->buzzer_id);
    if (!buzzer) return ESP_ERR_INVALID_ARG;

    if (s_playback.active) {
        esp_timer_stop(s_timer);
        s_playback.active = false;
    }

    return buzzer_driver_stop(buzzer);
}

static esp_err_t cmd_buzzer_beep(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const buzzer_beep_params_t *p = &params->buzzer_beep;
    buzzer_handle_t buzzer = get_buzzer(p->buzzer_id);
    if (!buzzer) return ESP_ERR_INVALID_ARG;

    if (s_playback.active) {
        esp_timer_stop(s_timer);
        s_playback.active = false;
    }

    s_playback.buzzer_id = p->buzzer_id;
    s_playback.active = true;
    s_playback.is_pattern = false;
    s_playback.freq_hz = p->frequency_hz;
    s_playback.duty = p->duty_percent;
    s_playback.duration_ms = p->duration_ms;

    buzzer_driver_start_tone(buzzer, p->frequency_hz, p->duty_percent);
    esp_timer_start_once(s_timer, p->duration_ms * 1000);

    return ESP_OK;
}

static esp_err_t cmd_buzzer_pattern(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const buzzer_pattern_params_t *p = &params->buzzer_pattern;
    buzzer_handle_t buzzer = get_buzzer(p->buzzer_id);
    if (!buzzer) return ESP_ERR_INVALID_ARG;

    if (s_playback.active) {
        esp_timer_stop(s_timer);
        s_playback.active = false;
    }

    const uint16_t *pattern = NULL;
    uint32_t pattern_len = 0;

    switch (p->pattern_id) {
        case 0:  pattern    = s_pattern_attention;          break;
        case 1:  pattern    = s_pattern_success;            break;
        case 2:  pattern    = s_pattern_error;              break;
        case 3:  pattern    = s_pattern_full;               break;
        case 4:  pattern    = s_pattern_friendly_chirp;     break;
        case 5:  pattern    = s_pattern_success_arpeggio;   break;
        case 6:  pattern    = s_pattern_gentle_fade;        break;
        case 7:  pattern    = s_pattern_warning_pulse;      break;
        case 8:  pattern    = s_pattern_urgent_alarm;       break;
        case 9:  pattern    = s_pattern_escalating_siren;   break;
        case 10: pattern    = s_pattern_calming_chord;      break;
        case 11: pattern    = s_pattern_error_glissando;    break;
        case 12: pattern    = s_pattern_power_up;           break;

        default: return     ESP_ERR_INVALID_ARG;
    }

    /* Determine pattern length (count of uint16_t elements until 0,0) */
    uint32_t len = 0;
    const uint16_t *ptr = pattern;
    while (ptr[0] != 0 || ptr[1] != 0) {
        len += 2;
        ptr += 2;
    }
    pattern_len = len;

    s_playback.buzzer_id = p->buzzer_id;
    s_playback.active = true;
    s_playback.is_pattern = true;
    s_playback.pattern = pattern;
    s_playback.pattern_len = pattern_len;
    s_playback.pattern_idx = 0;
    s_playback.duty = 50;  /* fixed duty for patterns */

    /* Start the first step */
    playback_timer_callback(NULL);

    return ESP_OK;
}

static esp_err_t cmd_buzzer_stop(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const buzzer_off_params_t *p = &params->buzzer_off;
    buzzer_handle_t buzzer = get_buzzer(p->buzzer_id);
    if (!buzzer) return ESP_ERR_INVALID_ARG;

    if (s_playback.active) {
        esp_timer_stop(s_timer);
        s_playback.active = false;
    }
    return buzzer_driver_stop(buzzer);
}

/* Wrappers for command router (void* params) */
static esp_err_t buzzer_on_wrapper(void *context, void *params)
{
    return cmd_buzzer_on(context, (const command_param_union_t*)params);
}
static esp_err_t buzzer_off_wrapper(void *context, void *params)
{
    return cmd_buzzer_off(context, (const command_param_union_t*)params);
}
static esp_err_t buzzer_beep_wrapper(void *context, void *params)
{
    return cmd_buzzer_beep(context, (const command_param_union_t*)params);
}
static esp_err_t buzzer_pattern_wrapper(void *context, void *params)
{
    return cmd_buzzer_pattern(context, (const command_param_union_t*)params);
}
static esp_err_t buzzer_stop_wrapper(void *context, void *params)
{
    return cmd_buzzer_stop(context, (const command_param_union_t*)params);
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t buzzer_service_init(void)
{
    for (int i = 0; i < BUZZER_COUNT; i++) {
        esp_err_t ret = buzzer_driver_create(&s_buzzer_configs[i], &s_buzzers[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create buzzer %d: %d", i, ret);
            return ret;
        }
    }

    esp_timer_create_args_t timer_args = {
        .callback = playback_timer_callback,
        .arg = NULL,
        .name = "buzzer_playback"
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Buzzer service initialised (%d buzzers)", BUZZER_COUNT);
    return ESP_OK;
}

esp_err_t buzzer_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_BUZZER_ON, buzzer_on_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_BUZZER_OFF, buzzer_off_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_BUZZER_BEEP, buzzer_beep_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_BUZZER_PATTERN, buzzer_pattern_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_BUZZER_STOP, buzzer_stop_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Buzzer command handlers registered");
    return ESP_OK;
}

esp_err_t buzzer_service_start(void)
{
    ESP_LOGI(TAG, "Buzzer service started");
    return ESP_OK;
}