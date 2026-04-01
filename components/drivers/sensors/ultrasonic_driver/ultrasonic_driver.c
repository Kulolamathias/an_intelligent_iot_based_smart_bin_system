#if 0


/**
 * @file ultrasonic_driver.c
 * @brief Ultrasonic driver implementation.
 *
 * =============================================================================
 * Uses esp_timer_get_time() for microsecond precision.
 * Trigger pulse: 10µs high on trig pin.
 * Echo measurement: measures high pulse width on echo pin.
 * Timeout handled via busy-wait with esp_timer.
 * =============================================================================
 */

#include "ultrasonic_driver.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ULTRASONIC_DRV";

#define TRIGGER_PULSE_US 10

/**
 * Internal handle structure.
 */
struct ultrasonic_handle_t {
    gpio_num_t trig_pin;
    gpio_num_t echo_pin;
    uint32_t timeout_us;
};

esp_err_t ultrasonic_driver_create(const ultrasonic_config_t *cfg,
                                   ultrasonic_handle_t *out_handle)
{
    if (cfg == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate handle
    struct ultrasonic_handle_t *handle = calloc(1, sizeof(struct ultrasonic_handle_t));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->trig_pin = cfg->trig_pin;
    handle->echo_pin = cfg->echo_pin;
    handle->timeout_us = cfg->timeout_us;

    // Configure GPIOs
    gpio_config_t trig_conf = {
        .pin_bit_mask   = (1ULL << handle->trig_pin),
        .mode           = GPIO_MODE_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&trig_conf);

    gpio_config_t echo_conf = {
        .pin_bit_mask = (1ULL << handle->echo_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&echo_conf);

    // Ensure trigger is low
    gpio_set_level(handle->trig_pin, 0);

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t ultrasonic_driver_measure(ultrasonic_handle_t handle,
                                    uint32_t *pulse_width_us)
{
    if (handle == NULL || pulse_width_us == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Send trigger pulse: 10µs high
    gpio_set_level(handle->trig_pin, 1);
    esp_rom_delay_us(TRIGGER_PULSE_US);
    gpio_set_level(handle->trig_pin, 0);

    // Wait for echo pin to go high (start of pulse)
    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(handle->echo_pin) == 0) {
        if ((esp_timer_get_time() - start_time) > handle->timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    // Measure pulse width
    int64_t pulse_start = esp_timer_get_time();
    while (gpio_get_level(handle->echo_pin) == 1) {
        if ((esp_timer_get_time() - pulse_start) > handle->timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }
    int64_t pulse_end = esp_timer_get_time();

    *pulse_width_us = (uint32_t)(pulse_end - pulse_start);
    return ESP_OK;
}

esp_err_t ultrasonic_driver_delete(ultrasonic_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    free(handle);
    return ESP_OK;
}



#else



/**
 * @file ultrasonic_driver.c
 * @brief Ultrasonic driver – interrupt‑based, multi‑instance.
 *        Uses task notification instead of semaphore to avoid spinlock issues.
 */

#include "ultrasonic_driver.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "ULTRASONIC_DRV";
static bool s_isr_service_installed = false;

/* We'll assign timer group/ID per instance. */
#define MAX_SENSORS 2
static int s_next_timer_id = 0;

struct ultrasonic_handle_t {
    gpio_num_t trig_pin;
    gpio_num_t echo_pin;
    uint32_t timeout_us;
    int timer_group;
    int timer_id;
    TaskHandle_t waiting_task;   /* task waiting for measurement */
    volatile uint32_t pulse_width;
    volatile bool done;
    volatile bool edge_rising;
};

/* GPIO ISR handler – called on rising and falling edges */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    ultrasonic_handle_t handle = (ultrasonic_handle_t)arg;
    uint32_t level = gpio_get_level(handle->echo_pin);
    if (level) {
        /* Rising edge: start timer */
        timer_set_counter_value(handle->timer_group, handle->timer_id, 0);
        timer_start(handle->timer_group, handle->timer_id);
        handle->edge_rising = true;
    } else if (handle->edge_rising) {
        /* Falling edge: stop timer and capture value */
        uint64_t value;
        timer_get_counter_value(handle->timer_group, handle->timer_id, &value);
        handle->pulse_width = (uint32_t)value;  /* microseconds */
        timer_pause(handle->timer_group, handle->timer_id);
        handle->done = true;
        if (handle->waiting_task) {
            BaseType_t higher_task_woken = pdFALSE;
            vTaskNotifyGiveFromISR(handle->waiting_task, &higher_task_woken);
            portYIELD_FROM_ISR(higher_task_woken);
        }
        handle->edge_rising = false;
    }
}

esp_err_t ultrasonic_driver_create(const ultrasonic_config_t *cfg,
                                   ultrasonic_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;
    if (s_next_timer_id >= 2) {
        ESP_LOGE(TAG, "Only 2 sensors supported with this simple timer allocation");
        return ESP_ERR_NOT_SUPPORTED;
    }

    ultrasonic_handle_t handle = calloc(1, sizeof(struct ultrasonic_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->trig_pin = cfg->trig_pin;
    handle->echo_pin = cfg->echo_pin;
    handle->timeout_us = cfg->timeout_us;
    handle->timer_group = TIMER_GROUP_0;
    handle->timer_id = s_next_timer_id++;
    handle->waiting_task = NULL;
    handle->done = false;
    handle->edge_rising = false;

    /* Configure trigger pin as output */
    gpio_config_t trig_conf = {
        .pin_bit_mask = (1ULL << handle->trig_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&trig_conf);
    if (ret != ESP_OK) goto fail;

    /* Configure echo pin as input with interrupt on both edges */
    gpio_config_t echo_conf = {
        .pin_bit_mask = (1ULL << handle->echo_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ret = gpio_config(&echo_conf);
    if (ret != ESP_OK) goto fail;

    /* Install global ISR service if not already done */
    if (!s_isr_service_installed) {
        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install ISR service");
            goto fail;
        }
        s_isr_service_installed = true;
    }

    /* Add ISR handler for this echo pin */
    ret = gpio_isr_handler_add(handle->echo_pin, gpio_isr_handler, handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler");
        goto fail;
    }

    /* Configure timer for this instance */
    timer_config_t timer_cfg = {
        .divider = 80,          /* 1 µs per tick */
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_DIS,
        .auto_reload = TIMER_AUTORELOAD_DIS,
    };
    ret = timer_init(handle->timer_group, handle->timer_id, &timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init timer %d", handle->timer_id);
        goto fail;
    }

    /* Initialise state */
    gpio_set_level(handle->trig_pin, 0);

    *out_handle = handle;
    ESP_LOGI(TAG, "Ultrasonic driver initialised (trig=%d, echo=%d, timer=%d)",
             handle->trig_pin, handle->echo_pin, handle->timer_id);
    return ESP_OK;

fail:
    if (handle->echo_pin) gpio_isr_handler_remove(handle->echo_pin);
    free(handle);
    return ret;
}

esp_err_t ultrasonic_driver_measure(ultrasonic_handle_t handle,
                                    uint32_t *pulse_width_us)
{
    if (!handle || !pulse_width_us) return ESP_ERR_INVALID_ARG;

    /* Reset state */
    handle->done = false;
    handle->pulse_width = 0;
    handle->edge_rising = false;
    handle->waiting_task = xTaskGetCurrentTaskHandle();

    /* Small delay to let any previous echo settle */
    vTaskDelay(pdMS_TO_TICKS(1));

    /* Send trigger pulse (20 µs) */
    gpio_set_level(handle->trig_pin, 1);
    esp_rom_delay_us(20);
    gpio_set_level(handle->trig_pin, 0);

    /* Wait for measurement to complete or timeout */
    uint32_t timeout_ms = handle->timeout_us / 1000;
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) > 0) {
        /* Notification received */
        if (handle->pulse_width < 10) {
            /* Very short pulses are likely noise */
            ESP_LOGW(TAG, "Pulse too short: %lu us, ignoring", handle->pulse_width);
            handle->waiting_task = NULL;
            return ESP_ERR_TIMEOUT;
        }
        *pulse_width_us = handle->pulse_width;
        handle->waiting_task = NULL;
        return ESP_OK;
    } else {
        /* Timeout: stop timer and clean up */
        timer_pause(handle->timer_group, handle->timer_id);
        handle->done = true;
        handle->waiting_task = NULL;
        ESP_LOGW(TAG, "Echo timeout");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t ultrasonic_driver_delete(ultrasonic_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    gpio_isr_handler_remove(handle->echo_pin);
    timer_pause(handle->timer_group, handle->timer_id);
    timer_deinit(handle->timer_group, handle->timer_id);
    free(handle);
    return ESP_OK;
}


#endif