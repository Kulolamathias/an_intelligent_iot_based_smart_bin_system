/**
 * @file tests/src/wifi_mqtt_test.c
 * @brief WiFi and MQTT service test – implementation.
 *
 * =============================================================================
 * TEST SEQUENCE
 * =============================================================================
 * 1. Connect to WiFi.
 * 2. Wait for EVENT_WIFI_GOT_IP (or timeout).
 * 3. Connect to MQTT broker.
 * 4. Wait for EVENT_MQTT_CONNECTED.
 * 5. Build device-specific topics using mqtt_topic.
 * 6. Subscribe to a test command topic.
 * 7. Publish an online status message.
 * 8. Wait a bit to allow messages to be received.
 * 9. (Optionally) disconnect MQTT and WiFi.
 *
 * =============================================================================
 * Note: For debugging, the test prints logs but does not block on events.
 *       The test passes if commands are sent without error.
 *
 * @author matthithyahu
 * @date 2026
 */

#include "wifi_mqtt_test.h"
#include "command_router.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_efuse.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_topic.h"
#include <string.h>

static const char *TAG = "WifiMqttTest";

/* Configuration – should match your network */
#define CONFIG_WIFI_SSID "Mathias' Sxx U..."
#define CONFIG_WIFI_PASSWORD "1234567890223"
#define CONFIG_MQTT_BROKER_URI "mqtt://102.223.8.140:1883"
#define CONFIG_MQTT_USERNAME "mqtt_user"
#define CONFIG_MQTT_PASSWORD "ega12345"

/* Timeout values (ms) */
#define WIFI_CONNECT_TIMEOUT_MS   10000
#define MQTT_CONNECT_TIMEOUT_MS    5000

esp_err_t wifi_mqtt_test_run(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting WiFi + MQTT test");

    /* 1. Connect to WiFi */
    cmd_connect_wifi_params_t wifi_conn = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
        .auth_mode = 0
    };
    command_param_union_t wifi_params;
    memcpy(&wifi_params.connect_wifi, &wifi_conn, sizeof(cmd_connect_wifi_params_t));
    ret = command_router_execute(CMD_CONNECT_WIFI, &wifi_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "COMMAND_CONNECT_WIFI failed: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "WiFi connection command sent. Waiting for connection...");
    vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    ESP_LOGI(TAG, "Assuming WiFi connected (check logs).");

    /* 2. Inform MQTT service that WiFi is now connected */
    uint32_t wifi_state = 1;
    ret = command_router_execute(CMD_MQTT_SET_WIFI_STATE, &wifi_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CMD_MQTT_SET_WIFI_STATE failed: %d", ret);
        return ret;
    }

    /* 3. Connect to MQTT broker */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char client_id[20];
    snprintf(client_id, sizeof(client_id), "bin_%s", mac_str);

    cmd_connect_mqtt_params_t mqtt_conn = {
        .broker_uri = CONFIG_MQTT_BROKER_URI,
        .client_id = "",
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

    command_param_union_t mqtt_params;
    memcpy(&mqtt_params.connect_mqtt, &mqtt_conn, sizeof(cmd_connect_mqtt_params_t));
    ret = command_router_execute(CMD_CONNECT_MQTT, &mqtt_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "COMMAND_CONNECT_MQTT failed: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "MQTT connection command sent. Waiting for connection...");
    vTaskDelay(pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS));
    ESP_LOGI(TAG, "Assuming MQTT connected (check logs).");

    /* 4. Build topics and subscribe/publish */
    char base_topic[32];
    ret = mqtt_topic_init(base_topic, sizeof(base_topic));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mqtt_topic_init failed: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Base topic: %s", base_topic);

    char sub_topic[128];
    ret = mqtt_topic_build(sub_topic, sizeof(sub_topic), "cmd/test");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mqtt_topic_build failed: %d", ret);
        return ret;
    }
    cmd_subscribe_mqtt_params_t sub = {
        .topic = "",
        .qos = 1
    };
    strlcpy(sub.topic, sub_topic, sizeof(sub.topic));
    command_param_union_t sub_params;
    memcpy(&sub_params.subscribe_mqtt, &sub, sizeof(cmd_subscribe_mqtt_params_t));
    ret = command_router_execute(CMD_SUBSCRIBE_MQTT, &sub_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "COMMAND_SUBSCRIBE_MQTT failed: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Subscribed to %s", sub_topic);

    char pub_topic[128];
    ret = mqtt_topic_build(pub_topic, sizeof(pub_topic), "status/online");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mqtt_topic_build failed: %d", ret);
        return ret;
    }
    cmd_publish_mqtt_params_t pub = {
        .topic = "",
        .payload = "online",
        .payload_len = strlen("online"),
        .qos = 1,
        .retain = true
    };
    strlcpy(pub.topic, pub_topic, sizeof(pub.topic));
    command_param_union_t pub_params;
    memcpy(&pub_params.publish_mqtt, &pub, sizeof(cmd_publish_mqtt_params_t));
    ret = command_router_execute(CMD_PUBLISH_MQTT, &pub_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "COMMAND_PUBLISH_MQTT failed: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Published online status to %s", pub_topic);

    /* 5. Wait a moment to see if any messages arrive */
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "WiFi + MQTT test completed.");
    return ESP_OK;
}