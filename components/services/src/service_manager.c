/**
 * @file components/services/src/service_manager.c
 * @brief Service Manager – implementation.
 *
 * =============================================================================
 * This file contains the static service registry and iterates through it
 * for init, register, and start phases. The order is determined by the
 * order of entries in the s_services array.
 * =============================================================================
 */

#include "service_manager.h"
#include "esp_log.h"
#include "example_service.h"          // placeholder for real services

#include "timer_service.h"
#include "indicator_service.h"
#include "led_service.h"
#include "buzzer_service.h"
#include "lcd_service.h"
#include "gsm_service.h"
#include "gps_service.h"
#include "pir_service.h"
#include "ultrasonic_service.h"
#include "servo_service.h"
#include "wifi_service.h"
#include "mqtt_service.h"
#include "bin_network_service.h"
#include "web_command_service.h"


static const char *TAG = "SERVICE_MGR";

/* ============================================================
 * Service Entry Structure
 * ============================================================ */
typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*register_handlers)(void);
    esp_err_t (*start)(void);
} service_entry_t;

/* ============================================================
 * Static Service Registry
 *
 * Services are listed in the order they must be initialised,
 * registered, and started. This order is fixed at compile time.
 * ============================================================ */
static const service_entry_t s_services[] = {
    {
        .name = "gps",
        .init = gps_service_init,
        .register_handlers = gps_service_register_handlers,
        .start = gps_service_start
    },
    {
        .name = "timer",
        .init = timer_service_init,
        .register_handlers = timer_service_register_handlers,
        .start = timer_service_start
    },
    {
        .name = "lcd",
        .init = lcd_service_init,
        .register_handlers = lcd_service_register_handlers,
        .start = lcd_service_start
    },
    {
        .name = "led",
        .init = led_service_init,
        .register_handlers = led_service_register_handlers,
        .start = led_service_start
    },
        {
        .name = "buzzer",
        .init = buzzer_service_init,
        .register_handlers = buzzer_service_register_handlers,
        .start = buzzer_service_start
    },
    // {
    //     .name = "gsm",
    //     .init = gsm_service_init,
    //     .register_handlers = gsm_service_register_handlers,
    //     .start = gsm_service_start
    // },
    {
        .name = "wifi",
        .init = wifi_service_init,
        .register_handlers = wifi_service_register_handlers,
        .start = wifi_service_start
    },
    {
        .name = "mqtt",
        .init = mqtt_service_init,
        .register_handlers = mqtt_service_register_handlers,
        .start = mqtt_service_start
    },
    {
        .name = "bin_network",
        .init = bin_network_service_init,
        .register_handlers = bin_network_service_register_commands,
        .start = bin_network_service_start
    },
    {
        .name = "web_cmd",
        .init = web_command_service_init,
        .register_handlers = web_command_service_register_handlers,
        .start = web_command_service_start
    },
    {
        .name = "pir",
        .init = pir_service_init,
        .register_handlers = pir_service_register_handlers,
        .start = pir_service_start
    },
    {
        .name = "ultrasonic",
        .init = ultrasonic_service_init,
        .register_handlers = ultrasonic_service_register_handlers,
        .start = ultrasonic_service_start
    },
    {
        .name = "servo",
        .init = servo_service_init,
        .register_handlers = servo_service_register_handlers,
        .start = servo_service_start
    },
    // {
    //     .name = "example",
    //     .init = example_service_init,
    //     .register_handlers = example_service_register_handlers,
    //     .start = example_service_start
    // }
    // Future services will be added here in dependency order
};

#define NUM_SERVICES (sizeof(s_services) / sizeof(s_services[0]))

/* ============================================================
 * Internal Helper: iterate and call a phase function
 * ============================================================ */
static esp_err_t iterate_phase(const char *phase_name,
                               esp_err_t (*phase_fn)(const service_entry_t *))
{
    for (size_t i = 0; i < NUM_SERVICES; i++) {
        const service_entry_t *svc = &s_services[i];
        ESP_LOGI(TAG, "%s: %s", phase_name, svc->name);
        esp_err_t ret = phase_fn(svc);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "%s failed for %s: %d", phase_name, svc->name, ret);
            return ret;
        }
    }
    return ESP_OK;
}

static esp_err_t phase_init(const service_entry_t *svc) {
    return svc->init();
}

static esp_err_t phase_register(const service_entry_t *svc) {
    return svc->register_handlers();
}

static esp_err_t phase_start(const service_entry_t *svc) {
    return svc->start();
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t service_manager_init_all(void)
{
    ESP_LOGI(TAG, "Initialising all services");
    return iterate_phase("init", phase_init);
}

esp_err_t service_manager_register_all(void)
{
    ESP_LOGI(TAG, "Registering all service command handlers");
    return iterate_phase("register", phase_register);
}

esp_err_t service_manager_start_all(void)
{
    ESP_LOGI(TAG, "Starting all services");
    return iterate_phase("start", phase_start);
}