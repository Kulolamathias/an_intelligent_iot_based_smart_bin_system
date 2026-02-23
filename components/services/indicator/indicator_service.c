/**
 * @file indicator_service.c
 * @brief Indicator Service implementation.
 *
 * =============================================================================
 * Manages LED, buzzer, and LCD backlight via pattern commands.
 * Uses one FreeRTOS timer per indicator for blinking.
 * All state is protected by a mutex to avoid concurrency issues.
 * =============================================================================
 */

#include "indicator_service.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

/* Hardware driver headers */
#include "led_driver.h"
#include "buzzer_driver.h"
#include "lcd_driver.h"          /* provides lcd_init, lcd_get_handle, lcd_backlight_set(bool) */

static const char *TAG = "INDICATOR_SVC";

/* ============================================================
 * Pattern definitions
 * ============================================================ */
#define PATTERN_OFF         0
#define PATTERN_SOLID       1
#define PATTERN_SLOW_BLINK  2   /* 500 ms on/off */
#define PATTERN_FAST_BLINK  3   /* 200 ms on/off */

#define SLOW_BLINK_MS       500
#define FAST_BLINK_MS       200

/* ============================================================
 * Per‑indicator state
 * ============================================================ */
typedef struct {
    uint8_t         pattern;        /**< current active pattern */
    bool            state;          /**< current on/off state (for blinking) */
    TimerHandle_t   timer;          /**< timer for this indicator */
    void            (*set_fn)(bool);/**< driver function to set on/off */
} indicator_ctx_t;

/* ============================================================
 * Static service state
 * ============================================================ */
static indicator_ctx_t s_led = {
    .pattern = PATTERN_OFF,
    .state = false,
    .timer = NULL,
    .set_fn = led_set
};

static indicator_ctx_t s_buzzer = {
    .pattern = PATTERN_OFF,
    .state = false,
    .timer = NULL,
    .set_fn = buzzer_set
};

static indicator_ctx_t s_lcd = {
    .pattern = PATTERN_OFF,
    .state = false,
    .timer = NULL,
    .set_fn = NULL      /* will be set after LCD init */
};

static SemaphoreHandle_t s_mutex = NULL;
static bool s_drivers_initialised = false;

/* LCD handle – obtained from driver after init */
static void* s_lcd_handle = NULL;

/* Wrapper for LCD backlight control (matches void(*)(bool) signature) */
static void lcd_backlight_wrapper(bool state)
{
    if (s_lcd_handle) {
        lcd_backlight_set(state);   /* driver function now takes bool */
    }
}

/* ============================================================
 * Timer callback (common for all indicators)
 * ============================================================ */
static void indicator_timer_callback(TimerHandle_t xTimer)
{
    /* Retrieve the indicator context from the timer ID */
    indicator_ctx_t *ctx = (indicator_ctx_t*) pvTimerGetTimerID(xTimer);
    if (ctx == NULL) return;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        /* Toggle state */
        ctx->state = !ctx->state;
        if (ctx->set_fn) {
            ctx->set_fn(ctx->state);
        }

        /* Determine next timeout based on pattern */
        uint32_t timeout_ms = 0;
        switch (ctx->pattern) {
            case PATTERN_SLOW_BLINK:
                timeout_ms = SLOW_BLINK_MS;
                break;
            case PATTERN_FAST_BLINK:
                timeout_ms = FAST_BLINK_MS;
                break;
            default:
                /* Should not happen: timer should have been stopped */
                timeout_ms = 0;
                break;
        }

        if (timeout_ms > 0) {
            /* Restart timer for next toggle */
            xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(timeout_ms), 0);
        } else {
            /* Pattern changed to non‑blink while callback was pending? */
            if (ctx->set_fn) {
                ctx->set_fn(false);
            }
        }
        xSemaphoreGive(s_mutex);
    }
}

/* ============================================================
 * Helper: apply a new pattern to an indicator
 * ============================================================ */
static void apply_pattern(indicator_ctx_t *ctx, uint8_t new_pattern)
{
    /* Stop current timer if running */
    xTimerStop(ctx->timer, 0);

    if (new_pattern == PATTERN_OFF) {
        ctx->pattern = PATTERN_OFF;
        ctx->state = false;
        if (ctx->set_fn) ctx->set_fn(false);
    }
    else if (new_pattern == PATTERN_SOLID) {
        ctx->pattern = PATTERN_SOLID;
        ctx->state = true;
        if (ctx->set_fn) ctx->set_fn(true);
    }
    else if (new_pattern == PATTERN_SLOW_BLINK || new_pattern == PATTERN_FAST_BLINK) {
        ctx->pattern = new_pattern;
        ctx->state = true;  /* start with on */
        if (ctx->set_fn) ctx->set_fn(true);
        /* Start timer with appropriate half‑period */
        uint32_t period = (new_pattern == PATTERN_SLOW_BLINK) ? SLOW_BLINK_MS : FAST_BLINK_MS;
        xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(period), 0);
    }
    else {
        /* Unknown pattern – treat as off */
        ESP_LOGW(TAG, "Unknown pattern %d, turning off", new_pattern);
        ctx->pattern = PATTERN_OFF;
        ctx->state = false;
        if (ctx->set_fn) ctx->set_fn(false);
    }
}

/* ============================================================
 * Command handlers
 * ============================================================ */

static esp_err_t handle_update_indicators(void *context, void *params)
{
    (void)context;
    if (params == NULL) {
        ESP_LOGE(TAG, "CMD_UPDATE_INDICATORS called without parameters");
        return ESP_ERR_INVALID_ARG;
    }

    cmd_update_indicators_params_t *p = (cmd_update_indicators_params_t*)params;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        apply_pattern(&s_led, p->led_pattern);
        apply_pattern(&s_buzzer, p->buzzer_pattern);
        apply_pattern(&s_lcd, p->lcd_pattern);
        xSemaphoreGive(s_mutex);
    }

    ESP_LOGI(TAG, "Indicators updated: led=%d, buzzer=%d, lcd=%d",
             p->led_pattern, p->buzzer_pattern, p->lcd_pattern);
    return ESP_OK;
}

/* For simplicity, other UI commands map to the same handler.
   In a full implementation they would have separate handlers. */
static esp_err_t handle_update_display(void *context, void *params)
{
    return handle_update_indicators(context, params);
}

static esp_err_t handle_play_sound(void *context, void *params)
{
    return handle_update_indicators(context, params);
}

static esp_err_t handle_blink_lcd_backlight(void *context, void *params)
{
    return handle_update_indicators(context, params);
}

static esp_err_t handle_show_message(void *context, void *params)
{
    return handle_update_indicators(context, params);
}

/* ============================================================
 * Base contract implementation
 * ============================================================ */

esp_err_t indicator_service_init(void)
{
    esp_err_t ret;

    /* Initialise hardware drivers */
    ret = led_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED driver init failed");
        return ret;
    }

    ret = buzzer_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Buzzer driver init failed");
        return ret;
    }

    /**
     * TODO: LCD driver init is currently a no‑op in lcd_driver.c, but we call it here for completeness.
     * In a real implementation, lcd_init() would set up the LCD and provide a handle that we store for backlight control.
     */
    /* Initialise LCD driver and obtain handle */
    // ret = lcd_init();   /* assume lcd_init() returns esp_err_t and sets internal handle */
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "LCD driver init failed");
    //     return ret;
    // }
    // s_lcd_handle = lcd_get_handle();   /* store for wrapper */
    // if (s_lcd_handle == NULL) {
    //     ESP_LOGE(TAG, "Failed to get LCD handle");
    //     return ESP_FAIL;
    // }
    s_lcd.set_fn = lcd_backlight_wrapper;   /* attach wrapper */

    /* Create mutex */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    /* Create timers (one per indicator) */
    s_led.timer = xTimerCreate(
        "led_tmr",
        pdMS_TO_TICKS(100),  /* dummy */
        pdFALSE,             /* one‑shot (re‑armed manually) */
        (void*)&s_led,
        indicator_timer_callback
    );

    s_buzzer.timer = xTimerCreate(
        "buzzer_tmr",
        pdMS_TO_TICKS(100),
        pdFALSE,
        (void*)&s_buzzer,
        indicator_timer_callback
    );

    s_lcd.timer = xTimerCreate(
        "lcd_tmr",
        pdMS_TO_TICKS(100),
        pdFALSE,
        (void*)&s_lcd,
        indicator_timer_callback
    );

    if (s_led.timer == NULL || s_buzzer.timer == NULL || s_lcd.timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timers");
        return ESP_FAIL;
    }

    s_drivers_initialised = true;
    ESP_LOGI(TAG, "Indicator service initialised");
    return ESP_OK;
}

esp_err_t indicator_service_start(void)
{
    /* All indicators start off; no action needed */
    ESP_LOGI(TAG, "Indicator service started");
    return ESP_OK;
}

esp_err_t indicator_service_stop(void)
{
    /* Turn off all indicators and stop timers */
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        apply_pattern(&s_led, PATTERN_OFF);
        apply_pattern(&s_buzzer, PATTERN_OFF);
        apply_pattern(&s_lcd, PATTERN_OFF);
        xSemaphoreGive(s_mutex);
    }
    ESP_LOGI(TAG, "Indicator service stopped");
    return ESP_OK;
}

esp_err_t indicator_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_UPDATE_INDICATORS, handle_update_indicators, NULL);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to register CMD_UPDATE_INDICATORS"); return ret; }

    ret = service_register_command(CMD_UPDATE_DISPLAY, handle_update_display, NULL);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to register CMD_UPDATE_DISPLAY"); return ret; }

    ret = service_register_command(CMD_PLAY_SOUND, handle_play_sound, NULL);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to register CMD_PLAY_SOUND"); return ret; }

    ret = service_register_command(CMD_BLINK_LCD_BACKLIGHT, handle_blink_lcd_backlight, NULL);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to register CMD_BLINK_LCD_BACKLIGHT"); return ret; }

    ret = service_register_command(CMD_SHOW_MESSAGE, handle_show_message, NULL);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to register CMD_SHOW_MESSAGE"); return ret; }

    ESP_LOGI(TAG, "Indicator command handlers registered");
    return ESP_OK;
}