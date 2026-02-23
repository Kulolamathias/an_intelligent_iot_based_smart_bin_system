/**
 * @file led_driver.c
 * @brief LED driver implementation.
 */

#include "led_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led_driver";

/* GPIO pin number for LED (active high) */
#define LED_GPIO_PIN    2

esp_err_t led_driver_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask   = (1ULL << LED_GPIO_PIN),
        .mode           = GPIO_MODE_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        /* Start with LED off */
        gpio_set_level(LED_GPIO_PIN, 0);
        ESP_LOGI(TAG, "LED driver initialised on GPIO %d", LED_GPIO_PIN);
    } else {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", LED_GPIO_PIN, esp_err_to_name(ret));
    }
    return ret;
}

void led_set(bool on)
{
    gpio_set_level(LED_GPIO_PIN, on ? 1 : 0);
}