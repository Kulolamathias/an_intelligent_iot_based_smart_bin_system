#if 1


/**
 * @file buzzer_demo.c
 * @brief Comprehensive buzzer driver demonstration.
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * This demo tests every feature of the buzzer driver using only the driver
 * itself (no core, no command router, no services). It serves as a reference
 * for future projects.
 *
 * =============================================================================
 * HARDWARE REQUIREMENTS
 * =============================================================================
 * - Passive buzzer (requires PWM) connected to GPIO23 (adjustable).
 * - Power supply (3.3V or 5V, depending on buzzer).
 * - For active buzzers, replace the driver with a simple GPIO on/off driver.
 *
 * =============================================================================
 * FEATURES DEMONSTRATED
 * =============================================================================
 * - Continuous tone (on/off)
 * - Single beep with variable frequency and duration
 * - Multiple beep sequences (doorbell, alarm, notification)
 * - Frequency sweep (ascending, descending)
 * - Predefined patterns (happy, sad, alert)
 * - Simple melody (Twinkle Twinkle)
 * - Happy Birthday melody
 * - Advanced patterns: arpeggios, ringtones, game sounds, siren, heartbeat,
 *   UFO, R2D2 chirps, Morse code (SOS)
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 * 1. Connect a passive buzzer to GPIO23 (or modify pin in config).
 * 2. Replace your main.c with this file (or rename and adjust CMakeLists.txt).
 * 3. Build and flash: idf.py build flash monitor
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-01
 * =============================================================================
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "buzzer_driver.h"

static const char *TAG = "BUZZER_DEMO";

/* ============================================================
 * Helper: delay in milliseconds
 * ============================================================ */
static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ============================================================
 * Helper: play a single beep with given frequency and duration
 * ============================================================ */
static void beep(buzzer_handle_t buzzer, uint32_t freq, uint32_t duration_ms)
{
    buzzer_driver_start_tone(buzzer, freq, 50);
    delay_ms(duration_ms);
    buzzer_driver_stop(buzzer);
    delay_ms(20);   // small gap between beeps
}

/* ============================================================
 * Helper: play a sequence of notes from an array
 * Each element is (freq, duration_ms) pairs; end with (0,0)
 * ============================================================ */
static void play_sequence(buzzer_handle_t buzzer, const uint16_t sequence[])
{
    for (int i = 0; sequence[i] != 0 || sequence[i+1] != 0; i += 2) {
        uint32_t freq = sequence[i];
        uint32_t dur = sequence[i+1];
        if (freq == 0) {
            /* Silence */
            buzzer_driver_stop(buzzer);
            delay_ms(dur);
        } else {
            buzzer_driver_start_tone(buzzer, freq, 50);
            delay_ms(dur);
            buzzer_driver_stop(buzzer);
            delay_ms(20);   // short gap between notes
        }
    }
}

/* ============================================================
 * DEMO 1: Basic continuous tones
 * ============================================================ */
static void demo_continuous_tones(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "--- Demo 1: Continuous tones ---");

    /* 1 kHz for 1 second */
    ESP_LOGI(TAG, "1 kHz tone (1 sec)");
    buzzer_driver_start_tone(buzzer, 1000, 50);
    delay_ms(1000);
    buzzer_driver_stop(buzzer);
    delay_ms(500);

    /* 2 kHz for 0.5 second */
    ESP_LOGI(TAG, "2 kHz tone (0.5 sec)");
    buzzer_driver_start_tone(buzzer, 2000, 50);
    delay_ms(500);
    buzzer_driver_stop(buzzer);
    delay_ms(500);

    /* 500 Hz for 2 seconds */
    ESP_LOGI(TAG, "500 Hz tone (2 sec)");
    buzzer_driver_start_tone(buzzer, 500, 50);
    delay_ms(2000);
    buzzer_driver_stop(buzzer);
    delay_ms(500);
}

/* ============================================================
 * DEMO 2: Single beeps with varying duration
 * ============================================================ */
static void demo_single_beeps(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "--- Demo 2: Single beeps ---");

    ESP_LOGI(TAG, "Short beep (200 ms)");
    beep(buzzer, 2000, 200);
    delay_ms(500);

    ESP_LOGI(TAG, "Medium beep (500 ms)");
    beep(buzzer, 2000, 500);
    delay_ms(500);

    ESP_LOGI(TAG, "Long beep (1 sec)");
    beep(buzzer, 2000, 1000);
    delay_ms(500);
}

/* ============================================================
 * DEMO 3: Beep sequences (doorbell, alarm, notification)
 * ============================================================ */
static void demo_sequences(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "--- Demo 3: Beep sequences ---");

    /* Doorbell: two short beeps (2 kHz, 200 ms, 300 ms pause) */
    ESP_LOGI(TAG, "Doorbell (ding-dong)");
    beep(buzzer, 2000, 200);
    delay_ms(300);
    beep(buzzer, 2000, 200);
    delay_ms(1000);

    /* Alarm: three short beeps with fast pause */
    ESP_LOGI(TAG, "Alarm (fast beeps)");
    for (int i = 0; i < 3; i++) {
        beep(buzzer, 2500, 150);
        delay_ms(100);
    }
    delay_ms(1000);

    /* Notification: one short, one long (like "ta-dah") */
    ESP_LOGI(TAG, "Notification (ta-dah)");
    beep(buzzer, 1500, 100);
    delay_ms(200);
    beep(buzzer, 2000, 300);
    delay_ms(1000);
}

/* ============================================================
 * DEMO 4: Frequency sweeps
 * ============================================================ */
static void demo_sweeps(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "--- Demo 4: Frequency sweeps ---");

    /* Ascending sweep from 500 Hz to 5 kHz over 2 seconds */
    ESP_LOGI(TAG, "Ascending sweep (500 Hz → 5000 Hz, 2 sec)");
    for (uint32_t freq = 500; freq <= 5000; freq += 100) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(20);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(500);

    /* Descending sweep from 5 kHz to 500 Hz over 2 seconds */
    ESP_LOGI(TAG, "Descending sweep (5000 Hz → 500 Hz, 2 sec)");
    for (uint32_t freq = 5000; freq >= 500; freq -= 100) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(20);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(1000);
}

/* ============================================================
 * DEMO 5: Predefined patterns (happy, sad, alert)
 * ============================================================ */
static void demo_patterns(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "--- Demo 5: Emotion patterns ---");

    /* Happy: rising chirp */
    ESP_LOGI(TAG, "Happy (rising chirp)");
    for (uint32_t freq = 1000; freq <= 3000; freq += 200) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(50);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(1000);

    /* Sad: descending glissando */
    ESP_LOGI(TAG, "Sad (descending glissando)");
    for (uint32_t freq = 3000; freq >= 800; freq -= 200) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(80);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(1000);

    /* Alert: fast alternating high-low */
    ESP_LOGI(TAG, "Alert (alternating high-low)");
    for (int i = 0; i < 6; i++) {
        beep(buzzer, 2500, 100);
        delay_ms(50);
        beep(buzzer, 1000, 100);
        delay_ms(50);
    }
    delay_ms(1000);
}

/* ============================================================
 * DEMO 6: Simple melody (Twinkle Twinkle Little Star)
 * Notes frequencies (Hz): C4=262, D4=294, E4=330, F4=349, G4=392, A4=440, B4=494, C5=523
 * ============================================================ */
static void demo_melody(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "--- Demo 6: Melody (Twinkle Twinkle) ---");

    /* Twinkle Twinkle Little Star (first line) */
    uint16_t melody[] = {
        262, 400, 262, 400, 392, 400, 392, 400,   // Twinkle, twinkle
        349, 400, 349, 400, 330, 400, 330, 400,   // little star
        294, 400, 294, 400, 262, 400, 262, 400,   // how I wonder
        392, 400, 392, 400, 349, 400, 349, 400,   // what you are
        330, 400, 330, 400, 294, 400, 294, 400,   // up above the
        262, 400, 262, 400, 392, 400, 392, 400,   // world so high
        349, 400, 349, 400, 330, 400, 330, 400,   // like a diamond
        294, 400, 294, 400, 262, 400, 262, 400,   // in the sky
        0,0
    };
    play_sequence(buzzer, melody);
    delay_ms(1000);
}

/* ============================================================
 * DEMO 7: Happy Birthday melody
 * ============================================================ */
static void demo_happy_birthday(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "--- Demo 7: Happy Birthday ---");

    uint16_t melody[] = {
        262, 400, 262, 400, 294, 400, 262, 400,   // Happy birthday to you
        349, 400, 330, 400, 0,    200,            // (pause)
        262, 400, 262, 400, 294, 400, 262, 400,   // Happy birthday to you
        392, 400, 349, 400, 0,    200,            // (pause)
        262, 400, 262, 400, 523, 400, 440, 400,   // Happy birthday dear [name]
        349, 400, 330, 400, 294, 400, 0,    200,  // (pause)
        466, 400, 466, 400, 440, 400, 349, 400,   // Happy birthday to you
        392, 400, 349, 400, 0,    0
    };
    play_sequence(buzzer, melody);
    delay_ms(1000);
}

/* ============================================================
 * DEMO 8: Impressive patterns – Arpeggios, Ringtones, Game sounds, etc.
 * ============================================================ */
static void demo_impressive(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "--- Demo 8: Impressive patterns ---");

    /* 8.1 Arpeggio of a major chord (C major: C-E-G-C) */
    ESP_LOGI(TAG, "Arpeggio (C major chord)");
    uint16_t arpeggio[] = {
        262, 300, 330, 300, 392, 300, 523, 500,   // C E G C
        0,0
    };
    play_sequence(buzzer, arpeggio);
    delay_ms(500);

    /* 8.2 Classic Nokia ringtone (Vivaldi's Four Seasons) */
    ESP_LOGI(TAG, "Nokia ringtone");
    uint16_t nokia[] = {
        392, 300, 392, 300, 392, 300, 392, 300,   // first part
        392, 300, 392, 300, 392, 300, 392, 300,
        523, 300, 523, 300, 523, 300, 523, 300,
        523, 300, 523, 300, 523, 300, 523, 300,
        392, 300, 392, 300, 392, 300, 392, 300,
        392, 300, 392, 300, 392, 300, 392, 300,
        0,0
    };
    play_sequence(buzzer, nokia);
    delay_ms(500);

    /* 8.3 iPhone ringtone (simplified) */
    ESP_LOGI(TAG, "iPhone ringtone");
    uint16_t iphone[] = {
        523, 200, 587, 200, 659, 200, 698, 200,   // rising
        784, 400, 698, 200, 659, 200, 587, 200,   // falling
        523, 200, 587, 200, 659, 200, 698, 200,
        784, 400, 698, 200, 659, 200, 587, 200,
        0,0
    };
    play_sequence(buzzer, iphone);
    delay_ms(500);

    /* 8.4 Coin sound (video game) */
    ESP_LOGI(TAG, "Coin sound");
    for (uint32_t freq = 800; freq <= 1600; freq += 50) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(10);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(500);

    /* 8.5 Power-up sound */
    ESP_LOGI(TAG, "Power-up (Mario mushroom)");
    for (uint32_t freq = 400; freq <= 2000; freq += 80) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(20);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(500);

    /* 8.6 Jump sound */
    ESP_LOGI(TAG, "Jump sound");
    buzzer_driver_start_tone(buzzer, 1000, 50);
    delay_ms(100);
    for (uint32_t freq = 1000; freq <= 1500; freq += 50) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(5);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(500);

    /* 8.7 Police siren (alternating high-low) */
    ESP_LOGI(TAG, "Police siren");
    for (int i = 0; i < 6; i++) {
        buzzer_driver_start_tone(buzzer, 800, 50);
        delay_ms(200);
        buzzer_driver_start_tone(buzzer, 1200, 50);
        delay_ms(200);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(500);

    /* 8.8 Heartbeat (slow then fast) */
    ESP_LOGI(TAG, "Heartbeat");
    for (int i = 0; i < 3; i++) {
        beep(buzzer, 200, 100);
        delay_ms(200);
    }
    for (int i = 0; i < 6; i++) {
        beep(buzzer, 200, 70);
        delay_ms(100);
    }
    delay_ms(500);

    /* 8.9 UFO / theremin glide */
    ESP_LOGI(TAG, "UFO theremin");
    for (uint32_t freq = 500; freq <= 2500; freq += 10) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(5);
    }
    for (uint32_t freq = 2500; freq >= 500; freq -= 10) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        delay_ms(5);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(500);

    /* 8.10 R2D2 chirps */
    ESP_LOGI(TAG, "R2D2 chirps");
    for (int i = 0; i < 4; i++) {
        beep(buzzer, 1000, 30);
        delay_ms(30);
        beep(buzzer, 1500, 30);
        delay_ms(30);
        beep(buzzer, 800, 30);
        delay_ms(30);
    }
    delay_ms(500);

    /* 8.11 Morse code: SOS (... --- ...) */
    ESP_LOGI(TAG, "SOS in Morse code");
    for (int i = 0; i < 3; i++) { beep(buzzer, 800, 100); delay_ms(100); }   // S: dot dot dot
    delay_ms(200);
    for (int i = 0; i < 3; i++) { beep(buzzer, 800, 300); delay_ms(100); }   // O: dash dash dash
    delay_ms(200);
    for (int i = 0; i < 3; i++) { beep(buzzer, 800, 100); delay_ms(100); }   // S
    delay_ms(1000);

    /* 8.12 Complex pattern: fade in + melody + fade out */
    ESP_LOGI(TAG, "Fade-in melody (Twinkle opening)");
    // Fade in
    for (uint8_t vol = 10; vol <= 50; vol += 5) {
        buzzer_driver_start_tone(buzzer, 262, vol);
        delay_ms(50);
    }
    buzzer_driver_start_tone(buzzer, 262, 50);
    delay_ms(300);
    buzzer_driver_start_tone(buzzer, 262, 50);
    delay_ms(300);
    buzzer_driver_start_tone(buzzer, 392, 50);
    delay_ms(300);
    buzzer_driver_start_tone(buzzer, 392, 50);
    delay_ms(300);
    // Fade out
    for (uint8_t vol = 50; vol >= 5; vol -= 5) {
        buzzer_driver_start_tone(buzzer, 349, vol);
        delay_ms(50);
    }
    buzzer_driver_stop(buzzer);
    delay_ms(1000);
}

/* ============================================================
 * Main entry point
 * ============================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Buzzer Driver Demonstration ===");
    ESP_LOGI(TAG, "Connect a passive buzzer to GPIO23 (or change pin in config)");

    /* ============================================================
     * Create buzzer instance (GPIO23, channel 0, timer 0)
     * ============================================================ */
    buzzer_config_t cfg = {
        .gpio_num = GPIO_NUM_23,    // change if your buzzer is on a different pin
        .channel = LEDC_CHANNEL_0,
        .timer = LEDC_TIMER_0,
        .default_freq_hz = 2000,
        .default_duty_percent = 50
    };
    buzzer_handle_t buzzer;
    esp_err_t ret = buzzer_driver_create(&cfg, &buzzer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create buzzer: %d", ret);
        return;
    }

    /* Run all demos in order */
    demo_continuous_tones(buzzer);
    demo_single_beeps(buzzer);
    demo_sequences(buzzer);
    demo_sweeps(buzzer);
    demo_patterns(buzzer);
    demo_melody(buzzer);
    demo_happy_birthday(buzzer);
    demo_impressive(buzzer);

    /* Final: a cheerful exit */
    ESP_LOGI(TAG, "Demo completed. Playing exit jingle...");
    beep(buzzer, 1000, 200);
    delay_ms(100);
    beep(buzzer, 1500, 200);
    delay_ms(100);
    beep(buzzer, 2000, 400);

    /* Clean up */
    buzzer_driver_delete(buzzer);
    ESP_LOGI(TAG, "Done.");
}





// /**
//  * @file test_buzzer.c
//  * @brief Standalone test for buzzer driver (PWM using LEDC).
//  */

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_log.h"
// #include "buzzer_driver.h"

// static const char *TAG = "BUZZER_TEST";

// void app_main(void)
// {
//     ESP_LOGI(TAG, "Starting buzzer test");

//     /* Configure buzzer on GPIO23 */
//     buzzer_config_t cfg = {
//         .gpio_num = GPIO_NUM_23,
//         .channel = LEDC_CHANNEL_0,
//         .timer = LEDC_TIMER_0,
//         .default_freq_hz = 2000,
//         .default_duty_percent = 50
//     };

//     buzzer_handle_t buzzer;
//     esp_err_t ret = buzzer_driver_create(&cfg, &buzzer);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to create buzzer: %d", ret);
//         return;
//     }

//     /* Test 1: Continuous 2 kHz tone for 1 second */
//     ESP_LOGI(TAG, "Playing 2 kHz tone for 1 second...");
//     buzzer_driver_start_tone(buzzer, 2000, 50);
//     vTaskDelay(pdMS_TO_TICKS(1000));
//     buzzer_driver_stop(buzzer);

//     /* Test 2: Three short beeps at 1 kHz */
//     ESP_LOGI(TAG, "Three short beeps...");
//     for (int i = 0; i < 3; i++) {
//         buzzer_driver_start_tone(buzzer, 1000, 50);
//         vTaskDelay(pdMS_TO_TICKS(200));
//         buzzer_driver_stop(buzzer);
//         vTaskDelay(pdMS_TO_TICKS(100));
//     }

//     /* Test 3: Sweep frequency from 500 Hz to 5 kHz over 2 seconds */
//     ESP_LOGI(TAG, "Frequency sweep 500 Hz → 5000 Hz...");
//     for (uint32_t freq = 500; freq <= 5000; freq += 100) {
//         buzzer_driver_start_tone(buzzer, freq, 50);
//         vTaskDelay(pdMS_TO_TICKS(20));
//     }
//     buzzer_driver_stop(buzzer);

//     ESP_LOGI(TAG, "Test finished, deleting buzzer...");
//     vTaskDelay(pdMS_TO_TICKS(1000));
//     buzzer_driver_delete(buzzer);
//     ESP_LOGI(TAG, "Done.");
// }






#else 


/**
 * @file buzzer_demo.c
 * @brief Comprehensive buzzer demonstration using the buzzer driver.
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * This demo tests all features of the buzzer driver and optionally the
 * buzzer service. It serves as a reference for integrating buzzers into any
 * project that uses the driver.
 *
 * =============================================================================
 * FEATURES DEMONSTRATED
 * =============================================================================
 * - Continuous tone (on/off)
 * - Single beep with adjustable frequency and duration
 * - Predefined patterns (attention, success, error, full)
 * - Frequency sweep
 * - Multiple patterns chained
 * - Using buzzer service commands (optional)
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-01
 * =============================================================================
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "buzzer_driver.h"
#include "command_router.h"   // only if using service commands
#include "service_interfaces.h" // only if using service

static const char *TAG = "BUZZER_DEMO";

/* ============================================================
 * Helper: short delay between tests
 * ============================================================ */
static void wait(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ============================================================
 * Test using only the driver (no core/service)
 * ============================================================ */
static void test_driver_only(buzzer_handle_t buzzer)
{
    ESP_LOGI(TAG, "=== Driver-only tests ===");

    /* 1. Continuous tone for 1 second at 2 kHz */
    ESP_LOGI(TAG, "1. Continuous 2 kHz tone, 1 sec");
    buzzer_driver_start_tone(buzzer, 2000, 50);
    wait(1000);
    buzzer_driver_stop(buzzer);
    wait(500);

    /* 2. Single beep: 1 kHz, 300 ms */
    ESP_LOGI(TAG, "2. Single beep: 1 kHz, 300 ms");
    buzzer_driver_start_tone(buzzer, 1000, 50);
    wait(300);
    buzzer_driver_stop(buzzer);
    wait(500);

    /* 3. Three short beeps (like a doorbell) */
    ESP_LOGI(TAG, "3. Three short beeps (2 kHz, 200 ms, 100 ms pause)");
    for (int i = 0; i < 3; i++) {
        buzzer_driver_start_tone(buzzer, 2000, 50);
        wait(200);
        buzzer_driver_stop(buzzer);
        wait(100);
    }
    wait(500);

    /* 4. Frequency sweep from 500 Hz to 5 kHz over 2 seconds */
    ESP_LOGI(TAG, "4. Frequency sweep 500 Hz → 5000 Hz (2 sec)");
    for (uint32_t freq = 500; freq <= 5000; freq += 100) {
        buzzer_driver_start_tone(buzzer, freq, 50);
        wait(20);
    }
    buzzer_driver_stop(buzzer);
    wait(1000);
}

/* ============================================================
 * Test using service commands (if core and service are present)
 * ============================================================ */
static void test_service_commands(void)
{
    ESP_LOGI(TAG, "=== Service-based tests ===");
    ESP_LOGI(TAG, "Note: These require core and buzzer service to be running.");
    ESP_LOGI(TAG, "If not, you will see command router errors – ignore if not using service.");

    /* Prepare command parameters */
    command_param_union_t params;

    /* 1. Continuous 2 kHz tone */
    buzzer_on_params_t on = { .buzzer_id = 0, .frequency_hz = 2000, .duty_percent = 50 };
    memcpy(&params.buzzer_on, &on, sizeof(on));
    command_router_execute(CMD_BUZZER_ON, &params);
    wait(1000);
    command_router_execute(CMD_BUZZER_OFF, &params);
    wait(500);

    /* 2. Single beep: 1 kHz, 300 ms */
    buzzer_beep_params_t beep = { .buzzer_id = 0, .frequency_hz = 1000, .duty_percent = 50, .duration_ms = 300 };
    memcpy(&params.buzzer_beep, &beep, sizeof(beep));
    command_router_execute(CMD_BUZZER_BEEP, &params);
    wait(1000);

    /* 3. Attention pattern (3 short beeps) */
    buzzer_pattern_params_t pattern = { .buzzer_id = 0, .pattern_id = 0 };
    memcpy(&params.buzzer_pattern, &pattern, sizeof(pattern));
    command_router_execute(CMD_BUZZER_PATTERN, &params);
    wait(2000);

    /* 4. Success pattern (one short high beep) */
    pattern.pattern_id = 1;
    memcpy(&params.buzzer_pattern, &pattern, sizeof(pattern));
    command_router_execute(CMD_BUZZER_PATTERN, &params);
    wait(1000);

    /* 5. Error pattern (long low beep) */
    pattern.pattern_id = 2;
    memcpy(&params.buzzer_pattern, &pattern, sizeof(pattern));
    command_router_execute(CMD_BUZZER_PATTERN, &params);
    wait(2000);
}

/* ============================================================
 * Main entry point
 * ============================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Buzzer Demonstration Started ===");

    /* ============================================================
     * Create buzzer instance (GPIO 23, channel 0, timer 0)
     * ============================================================ */
    buzzer_config_t cfg = {
        .gpio_num = GPIO_NUM_23,
        .channel = LEDC_CHANNEL_0,
        .timer = LEDC_TIMER_0,
        .default_freq_hz = 2000,
        .default_duty_percent = 50
    };
    buzzer_handle_t buzzer;
    esp_err_t ret = buzzer_driver_create(&cfg, &buzzer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create buzzer: %d", ret);
        return;
    }

    /* ============================================================
     * Run driver tests
     * ============================================================ */
    test_driver_only(buzzer);

    /* ============================================================
     * Optional: run service tests (if core is initialised)
     * We skip this if the core is not running (i.e., in this standalone demo,
     * command_router_init was not called). To avoid errors, we'll only run
     * service tests if we detect that command_router is initialised.
     * You can comment out the if block if you want to test service commands.
     * ============================================================ */
    // Uncomment if you have initialised the core elsewhere.
    // test_service_commands();

    /* ============================================================
     * Clean up
     * ============================================================ */
    ESP_LOGI(TAG, "Demo finished. Deleting buzzer...");
    buzzer_driver_delete(buzzer);
    ESP_LOGI(TAG, "Done.");
}


#endif