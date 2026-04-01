/**
 * @file tests/src/ultrasonic_test.c
 * @brief Ultrasonic test – uses the interrupt-based driver.
 */

#include "ultrasonic_test.h"
#include "ultrasonic_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UltrasonicTest";

/* Pin assignments (adjust to your hardware) */
#define SENSOR1_TRIG 12
#define SENSOR1_ECHO 13
#define SENSOR2_TRIG 2
#define SENSOR2_ECHO 4

static ultrasonic_handle_t s_sensor1 = NULL;
static ultrasonic_handle_t s_sensor2 = NULL;
static bool s_test_running = true;

static void measurement_task(void *arg)
{
    uint32_t pulse1, pulse2;
    esp_err_t ret1, ret2;

    while (s_test_running) {
        /* Measure sensor 1 */
        ret1 = ultrasonic_driver_measure(s_sensor1, &pulse1);
        /* Measure sensor 2 */
        ret2 = ultrasonic_driver_measure(s_sensor2, &pulse2);

        if (ret1 == ESP_OK) {
            uint32_t dist1 = (pulse1 * 1715) / 100000;
            ESP_LOGI(TAG, "Sensor1: %lu cm", dist1);
        } else {
            ESP_LOGW(TAG, "Sensor1: timeout");
        }

        if (ret2 == ESP_OK) {
            uint32_t dist2 = (pulse2 * 1715) / 100000;
            ESP_LOGI(TAG, "Sensor2: %lu cm", dist2);
        } else {
            ESP_LOGW(TAG, "Sensor2: timeout");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(NULL);
}

esp_err_t ultrasonic_test_run(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting ultrasonic test");

    /* Create sensor 1 */
    ultrasonic_config_t cfg1 = {
        .trig_pin = SENSOR1_TRIG,
        .echo_pin = SENSOR1_ECHO,
        .timeout_us = 100000,   /* 100 ms */
    };
    ret = ultrasonic_driver_create(&cfg1, &s_sensor1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sensor1: %d", ret);
        return ret;
    }

    /* Create sensor 2 */
    ultrasonic_config_t cfg2 = {
        .trig_pin = SENSOR2_TRIG,
        .echo_pin = SENSOR2_ECHO,
        .timeout_us = 100000,
    };
    ret = ultrasonic_driver_create(&cfg2, &s_sensor2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sensor2: %d", ret);
        ultrasonic_driver_delete(s_sensor1);
        return ret;
    }

    /* Start measurement task */
    xTaskCreate(measurement_task, "ultra_task", 4096, NULL, 5, NULL);

    /* Let it run for 20 seconds (adjust as needed) */
    vTaskDelay(pdMS_TO_TICKS(20000));

    /* Stop test */
    s_test_running = true;
    vTaskDelay(pdMS_TO_TICKS(1000)); /* give task time to exit */

    /* Clean up */
    // ultrasonic_driver_delete(s_sensor1);
    // ultrasonic_driver_delete(s_sensor2);

    ESP_LOGI(TAG, "Ultrasonic test completed");
    return ESP_OK;
}