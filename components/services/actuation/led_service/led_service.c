/**
 * @file led_service.c
 * @brief LED Service – implementation with parameter‑based LED selection.
 */

#include "led_service.h"
#include "led_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "esp_log.h"

static const char *TAG = "LED_SVC";

#define LED_COUNT 5

static led_handle_t s_leds[LED_COUNT] = {NULL};

static const led_config_t s_led_configs[LED_COUNT] = {
    { .gpio_num = GPIO_NUM_25, .channel = LEDC_CHANNEL_0, .timer = LEDC_TIMER_0, .freq_hz = 1000, .active_high = true },  // white
    { .gpio_num = GPIO_NUM_5,  .channel = LEDC_CHANNEL_1, .timer = LEDC_TIMER_0, .freq_hz = 1000, .active_high = true },  // green
    { .gpio_num = GPIO_NUM_33, .channel = LEDC_CHANNEL_2, .timer = LEDC_TIMER_0, .freq_hz = 1000, .active_high = true },  // yellow
    { .gpio_num = GPIO_NUM_26, .channel = LEDC_CHANNEL_3, .timer = LEDC_TIMER_0, .freq_hz = 1000, .active_high = true },  // red
    { .gpio_num = GPIO_NUM_32, .channel = LEDC_CHANNEL_4, .timer = LEDC_TIMER_0, .freq_hz = 1000, .active_high = true },  // blue
};

static led_handle_t get_led(uint8_t led_id)
{
    if (led_id >= LED_COUNT) return NULL;
    return s_leds[led_id];
}

/* Command handlers */
static esp_err_t cmd_led_on(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    led_handle_t led = get_led(params->led_id.led_id);
    if (!led) return ESP_ERR_INVALID_ARG;
    return led_driver_on(led);
}

static esp_err_t cmd_led_off(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    led_handle_t led = get_led(params->led_id.led_id);
    if (!led) return ESP_ERR_INVALID_ARG;
    return led_driver_off(led);
}

static esp_err_t cmd_led_toggle(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    led_handle_t led = get_led(params->led_id.led_id);
    if (!led) return ESP_ERR_INVALID_ARG;
    return led_driver_toggle(led);
}

static esp_err_t cmd_led_set_brightness(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const led_brightness_params_t *p = &params->led_brightness;
    led_handle_t led = get_led(p->led_id);
    if (!led) return ESP_ERR_INVALID_ARG;
    return led_driver_set_brightness(led, p->percent);
}

static esp_err_t cmd_led_blink(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const led_blink_params_t *p = &params->led_blink;
    led_handle_t led = get_led(p->led_id);
    if (!led) return ESP_ERR_INVALID_ARG;
    return led_driver_start_blink(led, p->period_ms, p->duty_percent);
}

static esp_err_t cmd_led_blink_stop(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    led_handle_t led = get_led(params->led_id.led_id);
    if (!led) return ESP_ERR_INVALID_ARG;
    return led_driver_stop_blink(led);
}

static esp_err_t cmd_led_fade(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const led_fade_params_t *p = &params->led_fade;
    led_handle_t led = get_led(p->led_id);
    if (!led) return ESP_ERR_INVALID_ARG;
    return led_driver_start_fade(led, p->target_percent, p->duration_ms);
}

/* Wrappers for command router (void* params) */
static esp_err_t led_on_wrapper(void *context, void *params)
{
    return cmd_led_on(context, (const command_param_union_t*)params);
}
static esp_err_t led_off_wrapper(void *context, void *params)
{
    return cmd_led_off(context, (const command_param_union_t*)params);
}
static esp_err_t led_toggle_wrapper(void *context, void *params)
{
    return cmd_led_toggle(context, (const command_param_union_t*)params);
}
static esp_err_t led_set_brightness_wrapper(void *context, void *params)
{
    return cmd_led_set_brightness(context, (const command_param_union_t*)params);
}
static esp_err_t led_blink_wrapper(void *context, void *params)
{
    return cmd_led_blink(context, (const command_param_union_t*)params);
}
static esp_err_t led_blink_stop_wrapper(void *context, void *params)
{
    return cmd_led_blink_stop(context, (const command_param_union_t*)params);
}
static esp_err_t led_fade_wrapper(void *context, void *params)
{
    return cmd_led_fade(context, (const command_param_union_t*)params);
}

/* Public API */
esp_err_t led_service_init(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        esp_err_t ret = led_driver_create(&s_led_configs[i], &s_leds[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create LED %d: %d", i, ret);
            return ret;
        }
    }
    ESP_LOGI(TAG, "LED service initialised (%d LEDs)", LED_COUNT);
    return ESP_OK;
}

esp_err_t led_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_LED_ON, led_on_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_LED_OFF, led_off_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_LED_TOGGLE, led_toggle_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_LED_SET_BRIGHTNESS, led_set_brightness_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_LED_BLINK, led_blink_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_LED_BLINK_STOP, led_blink_stop_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_LED_FADE, led_fade_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "LED command handlers registered");
    return ESP_OK;
}

esp_err_t led_service_start(void)
{
    ESP_LOGI(TAG, "LED service started");
    return ESP_OK;
}