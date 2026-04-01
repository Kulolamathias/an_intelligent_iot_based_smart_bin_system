/**
 * @file main.c //this i just kept the name to avoid build issues, but it should be renamed to example_main_mqtt_wifi.c
 * @brief Example application demonstrating WiFi and MQTT services integration.
 * @brief Integration Test for WiFi and MQTT Services
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * This test initializes the core subsystems and services, connects to a WiFi
 * network, then to an MQTT broker. It subscribes to a device‑specific topic
 * and publishes an online status. Received MQTT messages are printed to the log.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * - Demonstrates the correct lifecycle of services via the service manager.
 * - Uses command router to send commands to services.
 * - Registers a local event handler to log incoming network messages.
 *
 * =============================================================================
 * DEPENDENCIES
 * =============================================================================
 * - command_router, event_dispatcher, service_manager (core)
 * - wifi_service, mqtt_service, mqtt_topic (services)
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_efuse.h"
#include "esp_mac.h"

/* Core interfaces */
#include "command_router.h"
#include "event_dispatcher.h"
#include "service_manager.h"
#include "command_params.h"
#include "event_types.h"

/* Services */
#include "wifi_service.h"
#include "mqtt_service.h"
#include "mqtt_topic.h"

/* Configuration (should be moved to Kconfig / menuconfig) */
#define CONFIG_WIFI_SSID "Mathias' Sxx U..."
#define CONFIG_WIFI_PASSWORD "1234567890223"
#define CONFIG_MQTT_BROKER_URI "mqtt://102.223.8.140:1883"
#define CONFIG_MQTT_USERNAME "mqtt_user"
#define CONFIG_MQTT_PASSWORD "ega12345"

static const char *TAG = "MAIN";



void app_main(void)
{
    esp_err_t ret;

    /* --------------------------------------------------------------------
     * 1. Initialize core subsystems
     * -------------------------------------------------------------------- */
    ESP_LOGI(TAG, "Initialising command router");
    ret = command_router_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "command_router_init failed: %d", ret); return; }

    ESP_LOGI(TAG, "Initialising event dispatcher");
    ret = event_dispatcher_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret); return; }

    /* --------------------------------------------------------------------
     * 2. Initialize NVS (required by WiFi)
     * -------------------------------------------------------------------- */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* --------------------------------------------------------------------
     * 3. Initialize TCP/IP stack and default event loop
     * -------------------------------------------------------------------- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* --------------------------------------------------------------------
     * 4. Initialize all services via service manager
     * -------------------------------------------------------------------- */
    ret = service_manager_init_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_init_all failed: %d", ret);
        return;
    }

    ret = service_manager_register_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_register_all failed: %d", ret);
        return;
    }

    ret = service_manager_start_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_start_all failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------------------
     * 5. Connect to WiFi
     * -------------------------------------------------------------------- */
    cmd_connect_wifi_params_t wifi_conn = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
        .auth_mode = 0
    };
    ESP_LOGI(TAG, "Connecting to WiFi...");
    command_router_execute(CMD_CONNECT_WIFI, &wifi_conn);

    /* Simple delay – in a real application, use an event group to wait for EVENT_WIFI_GOT_IP */
    vTaskDelay(pdMS_TO_TICKS(10000));
    ESP_LOGI(TAG, "Assuming WiFi connected");

    /* Inform MQTT service that WiFi is now connected */
    uint32_t wifi_state = 1;
    command_router_execute(CMD_MQTT_SET_WIFI_STATE, &wifi_state);

    /* --------------------------------------------------------------------
     * 6. Connect to MQTT broker
     * -------------------------------------------------------------------- */

     /* Read MAC address to create unique client ID */
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char client_id[20];
    snprintf(client_id, sizeof(client_id), "bin_%s", mac_str);

    cmd_connect_mqtt_params_t mqtt_conn = {
        .broker_uri = CONFIG_MQTT_BROKER_URI,
        .client_id = "",               /* temporary, will be overwritten */
        .username = CONFIG_MQTT_USERNAME,
        .password = CONFIG_MQTT_PASSWORD,
        .keepalive = 120,
        .disable_clean_session = false,
        .lwt_qos = 0,
        .lwt_retain = false,
        .lwt_topic = "",
        .lwt_message = "",
        .min_retry_delay_ms = 2000,
        .max_retry_delay_ms = 30000,
        .max_retry_attempts = 5
    };
    strlcpy(mqtt_conn.client_id, client_id, sizeof(mqtt_conn.client_id));
    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    command_router_execute(CMD_CONNECT_MQTT, &mqtt_conn);

    /* Allow time for connection */
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* --------------------------------------------------------------------
     * 7. Build topics and subscribe/publish
     * -------------------------------------------------------------------- */
    char base_topic[32];
    ESP_ERROR_CHECK(mqtt_topic_init(base_topic, sizeof(base_topic)));
    ESP_LOGI(TAG, "Base topic: %s", base_topic);

    /* Subscribe to devices/<mac>/cmd/test */
    char sub_topic[128];
    ESP_ERROR_CHECK(mqtt_topic_build(sub_topic, sizeof(sub_topic), "cmd/test"));
    cmd_subscribe_mqtt_params_t sub = {
        .topic = "",
        .qos = 1
    };
    strlcpy(sub.topic, sub_topic, sizeof(sub.topic));
    ESP_LOGI(TAG, "Subscribing to %s", sub_topic);
    command_router_execute(CMD_SUBSCRIBE_MQTT, &sub);

    /* Publish to devices/<mac>/status/online */
    char pub_topic[128];
    ESP_ERROR_CHECK(mqtt_topic_build(pub_topic, sizeof(pub_topic), "status/online"));
    cmd_publish_mqtt_params_t pub = {
        .topic = "",
        .payload = "online",
        .payload_len = strlen("online"),
        .qos = 1,
        .retain = true
    };
    strlcpy(pub.topic, pub_topic, sizeof(pub.topic));
    ESP_LOGI(TAG, "Publishing %s", pub_topic);
    command_router_execute(CMD_PUBLISH_MQTT, &pub);

    /* --------------------------------------------------------------------
     * 8. Main loop – keep running
     * -------------------------------------------------------------------- */
    ESP_LOGI(TAG, "Test running. Check MQTT Explorer for messages.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
