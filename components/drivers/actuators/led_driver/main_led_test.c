#if 1


/**
 * @file main_led_test.c
 * @brief Standalone test for LED driver.
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * This test creates 5 LED instances on GPIO pins 5, 18, 19, 33, 32 and
 * demonstrates:
 * - On/off/toggle
 * - Brightness control (0‑100%)
 * - Blinking with different periods/duty cycles
 * - Smooth fade (brightness transition)
 *
 * It uses the LED driver directly (no service layer) and runs a test sequence
 * with delays. The system idles after the sequence.
 * =============================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "led_driver.h"

static const char *TAG = "LED_TEST";

/* LED configuration: 5 LEDs on different pins */
#define LED_COUNT 5

/* Pin assignments */
static const gpio_num_t s_led_pins[LED_COUNT] = {
    GPIO_NUM_5,
    GPIO_NUM_18,
    GPIO_NUM_19,
    GPIO_NUM_33,
    GPIO_NUM_32
};

/* Shared timer (all LEDs can use same timer if frequency matches) */
#define LEDC_TIMER_SEL LEDC_TIMER_0
#define PWM_FREQ_HZ 1000
#define ACTIVE_HIGH true

/* Store handles for each LED */
static led_handle_t s_leds[LED_COUNT] = {NULL};

/* Helper: create all LEDs */
static esp_err_t create_leds(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        led_config_t cfg = {
            .gpio_num = s_led_pins[i],
            .channel = i,               /* LEDC_CHANNEL_0 to 4 */
            .timer = LEDC_TIMER_SEL,
            .freq_hz = PWM_FREQ_HZ,
            .active_high = ACTIVE_HIGH
        };
        esp_err_t ret = led_driver_create(&cfg, &s_leds[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create LED %d on pin %d: %d", i, s_led_pins[i], ret);
            return ret;
        }
        ESP_LOGI(TAG, "LED %d created on GPIO %d", i, s_led_pins[i]);
    }
    return ESP_OK;
}

/* Helper: delete all LEDs */
static void delete_leds(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        if (s_leds[i]) {
            led_driver_delete(s_leds[i]);
            s_leds[i] = NULL;
        }
    }
}

/* Test sequence */
static void run_test_sequence(void)
{
    ESP_LOGI(TAG, "=== LED Driver Test Started ===");

    /* --------------------------------------------------------------------
     * 1. Basic on/off/toggle
     * -------------------------------------------------------------------- */
    ESP_LOGI(TAG, "1. Basic on/off/toggle (5s)");
    for (int i = 0; i < LED_COUNT; i++) {
        led_driver_on(s_leds[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (int i = 0; i < LED_COUNT; i++) {
        led_driver_off(s_leds[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    for (int i = 0; i < LED_COUNT; i++) {
        led_driver_on(s_leds[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (int i = 0; i < LED_COUNT; i++) {
        led_driver_toggle(s_leds[i]);  // should turn off
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (int i = 0; i < LED_COUNT; i++) {
        led_driver_toggle(s_leds[i]);  // should turn on
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (int i = 0; i < LED_COUNT; i++) {
        led_driver_off(s_leds[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* --------------------------------------------------------------------
     * 2. Brightness ramp
     * -------------------------------------------------------------------- */
    ESP_LOGI(TAG, "2. Brightness ramp (each LED ramps up then down)");
    for (int i = 0; i < LED_COUNT; i++) {
        // Ramp up from 0 to 100 in steps of 20
        for (uint8_t b = 0; b <= 100; b += 20) {
            led_driver_set_brightness(s_leds[i], b);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        // Ramp down from 100 to 0
        for (uint8_t b = 100; b > 0; b -= 20) {
            led_driver_set_brightness(s_leds[i], b);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        led_driver_off(s_leds[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* --------------------------------------------------------------------
     * 3. Blinking patterns
     * -------------------------------------------------------------------- */
    ESP_LOGI(TAG, "3. Blinking patterns (different periods/duty)");
    // LED0: slow blink (1s period, 50% duty)
    led_driver_start_blink(s_leds[0], 1000, 50);
    // LED1: fast blink (200ms period, 50% duty)
    led_driver_start_blink(s_leds[1], 200, 50);
    // LED2: very fast blink (100ms period, 50% duty)
    led_driver_start_blink(s_leds[2], 100, 50);
    // LED3: short on, long off (500ms period, 20% duty)
    led_driver_start_blink(s_leds[3], 500, 20);
    // LED4: long on, short off (500ms period, 80% duty)
    led_driver_start_blink(s_leds[4], 500, 80);
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Stop blinking on all
    for (int i = 0; i < LED_COUNT; i++) {
        led_driver_stop_blink(s_leds[i]);
        led_driver_off(s_leds[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* --------------------------------------------------------------------
     * 4. Fade transitions
     * -------------------------------------------------------------------- */
    ESP_LOGI(TAG, "4. Fade transitions (each LED fades up and down)");
    // LED0: fade from 0 to 100 in 1 second
    led_driver_start_fade(s_leds[0], 100, 1000);
    // LED1: fade from 0 to 100 in 2 seconds
    led_driver_start_fade(s_leds[1], 100, 2000);
    // LED2: fade from 0 to 50 in 1 second, then to 100 in another 1 second? We'll chain manually
    led_driver_start_fade(s_leds[2], 50, 1000);
    vTaskDelay(pdMS_TO_TICKS(1000));
    led_driver_start_fade(s_leds[2], 100, 1000);
    vTaskDelay(pdMS_TO_TICKS(1000));
    // LED3: fade from 0 to 80 in 1.5 seconds
    led_driver_start_fade(s_leds[3], 80, 1500);
    // LED4: fade from 0 to 100 in 2 seconds
    led_driver_start_fade(s_leds[4], 100, 2000);
    vTaskDelay(pdMS_TO_TICKS(3000)); // wait for all fades to complete

    // Fade back down
    led_driver_start_fade(s_leds[0], 0, 1000);
    led_driver_start_fade(s_leds[1], 0, 2000);
    led_driver_start_fade(s_leds[2], 0, 1000);
    led_driver_start_fade(s_leds[3], 0, 1500);
    led_driver_start_fade(s_leds[4], 0, 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "=== LED Driver Test Completed ===");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initialising LED driver...");

    /* Create LED instances */
    esp_err_t ret = create_leds();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LEDs, aborting");
        return;
    }

    /* Run test sequence */
    run_test_sequence();

    /* Clean up */
    delete_leds();

    ESP_LOGI(TAG, "Test finished. Idling...");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



#else



/**
 * @file main_led_test.c
 * @brief Standalone test for the LED driver (without core or services).
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * This test demonstrates all LED driver functions:
 *   - on/off, toggle
 *   - brightness setting (0-100%)
 *   - blinking with adjustable period and duty cycle
 *   - smooth fading
 *
 * It creates multiple LED instances (e.g., red, green, blue) and runs
 * a sequence of patterns to verify driver functionality.
 *
 * =============================================================================
 * DEPENDENCIES
 * =============================================================================
 * - led_driver.h / led_driver.c
 * - ESP‑IDF LEDC (included via driver)
 * - FreeRTOS (for vTaskDelay)
 *
 * =============================================================================
 * HARDWARE
 * =============================================================================
 * Adjust the GPIO pins and LEDC channels to match your wiring.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 * - Copy this file to your project's main/ directory.
 * - Replace main.c with this file (or rename accordingly).
 * - Build and flash.
 *
 * =============================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "led_driver.h"

static const char *TAG = "LED_TEST";

/* LED configurations – adjust GPIO and channel/timer as needed */
#define LED_RED_GPIO    2
#define LED_GREEN_GPIO  4
#define LED_BLUE_GPIO   5

/* LEDC resources – we share timer 0 for all LEDs (same frequency) */
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_CH_RED      LEDC_CHANNEL_0
#define LEDC_CH_GREEN    LEDC_CHANNEL_1
#define LEDC_CH_BLUE     LEDC_CHANNEL_2
#define LEDC_FREQ_HZ     1000      /* 1 kHz PWM for smooth dimming */

static led_handle_t s_red = NULL;
static led_handle_t s_green = NULL;
static led_handle_t s_blue = NULL;

/* Helper: print a separator */
static void print_separator(void)
{
    ESP_LOGI(TAG, "=========================================");
}

/* Test sequence */
static void run_test(void)
{
    print_separator();
    ESP_LOGI(TAG, "1. ON/OFF test (red, green, blue)");
    led_driver_on(s_red);
    vTaskDelay(pdMS_TO_TICKS(1000));
    led_driver_off(s_red);
    led_driver_on(s_green);
    vTaskDelay(pdMS_TO_TICKS(1000));
    led_driver_off(s_green);
    led_driver_on(s_blue);
    vTaskDelay(pdMS_TO_TICKS(1000));
    led_driver_off(s_blue);
    vTaskDelay(pdMS_TO_TICKS(500));

    print_separator();
    ESP_LOGI(TAG, "2. Toggle test (red)");
    for (int i = 0; i < 5; i++) {
        led_driver_toggle(s_red);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    led_driver_off(s_red);
    vTaskDelay(pdMS_TO_TICKS(500));

    print_separator();
    ESP_LOGI(TAG, "3. Brightness ramp (red 0→100%)");
    for (int p = 0; p <= 100; p += 10) {
        led_driver_set_brightness(s_red, p);
        ESP_LOGI(TAG, "Brightness: %d%%", p);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    led_driver_off(s_red);
    vTaskDelay(pdMS_TO_TICKS(500));

    print_separator();
    ESP_LOGI(TAG, "4. Blink test (green, 500ms period, 50% duty)");
    led_driver_start_blink(s_green, 500, 50);
    vTaskDelay(pdMS_TO_TICKS(3000));
    led_driver_stop_blink(s_green);
    led_driver_off(s_green);
    vTaskDelay(pdMS_TO_TICKS(500));

    print_separator();
    ESP_LOGI(TAG, "5. Blink with different duty cycles (blue)");
    led_driver_start_blink(s_blue, 1000, 20);  /* short on, long off */
    vTaskDelay(pdMS_TO_TICKS(3000));
    led_driver_start_blink(s_blue, 1000, 80);  /* long on, short off */
    vTaskDelay(pdMS_TO_TICKS(3000));
    led_driver_stop_blink(s_blue);
    led_driver_off(s_blue);
    vTaskDelay(pdMS_TO_TICKS(500));

    print_separator();
    ESP_LOGI(TAG, "6. Fade test (red → green → blue)");
    led_driver_set_brightness(s_red, 0);
    led_driver_set_brightness(s_green, 0);
    led_driver_set_brightness(s_blue, 0);
    led_driver_start_fade(s_red, 100, 1000);
    vTaskDelay(pdMS_TO_TICKS(1200));
    led_driver_start_fade(s_red, 0, 1000);
    led_driver_start_fade(s_green, 100, 1000);
    vTaskDelay(pdMS_TO_TICKS(1200));
    led_driver_start_fade(s_green, 0, 1000);
    led_driver_start_fade(s_blue, 100, 1000);
    vTaskDelay(pdMS_TO_TICKS(1200));
    led_driver_start_fade(s_blue, 0, 1000);
    vTaskDelay(pdMS_TO_TICKS(1200));

    print_separator();
    ESP_LOGI(TAG, "7. Simultaneous fade (rainbow effect)");
    led_driver_start_fade(s_red, 100, 2000);
    led_driver_start_fade(s_green, 100, 2000);
    led_driver_start_fade(s_blue, 100, 2000);
    vTaskDelay(pdMS_TO_TICKS(2500));
    led_driver_start_fade(s_red, 0, 2000);
    led_driver_start_fade(s_green, 0, 2000);
    led_driver_start_fade(s_blue, 0, 2000);
    vTaskDelay(pdMS_TO_TICKS(2500));

    print_separator();
    ESP_LOGI(TAG, "8. All off");
    led_driver_off(s_red);
    led_driver_off(s_green);
    led_driver_off(s_blue);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting LED driver test");

    /* Create LED instances */
    led_config_t cfg_red = {
        .gpio_num = LED_RED_GPIO,
        .channel = LEDC_CH_RED,
        .timer = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .active_high = true
    };
    esp_err_t ret = led_driver_create(&cfg_red, &s_red);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create red LED: %d", ret);
        return;
    }

    led_config_t cfg_green = {
        .gpio_num = LED_GREEN_GPIO,
        .channel = LEDC_CH_GREEN,
        .timer = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .active_high = true
    };
    ret = led_driver_create(&cfg_green, &s_green);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create green LED: %d", ret);
        led_driver_delete(s_red);
        return;
    }

    led_config_t cfg_blue = {
        .gpio_num = LED_BLUE_GPIO,
        .channel = LEDC_CH_BLUE,
        .timer = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .active_high = true
    };
    ret = led_driver_create(&cfg_blue, &s_blue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create blue LED: %d", ret);
        led_driver_delete(s_red);
        led_driver_delete(s_green);
        return;
    }

    ESP_LOGI(TAG, "All LEDs created successfully");

    /* Run the test sequence */
    run_test();

    /* Clean up */
    led_driver_delete(s_red);
    led_driver_delete(s_green);
    led_driver_delete(s_blue);

    ESP_LOGI(TAG, "Test completed. Restarting...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

#endif