

/**
 * @file main.c
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
#define CONFIG_MQTT_CLIENT_ID "your_client_id"
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
    cmd_connect_mqtt_params_t mqtt_conn = {
        .broker_uri = CONFIG_MQTT_BROKER_URI,
        .client_id = CONFIG_MQTT_CLIENT_ID,
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












// // #include <stdio.h>
// // #include <string.h>
// // #include "freertos/FreeRTOS.h"
// // #include "freertos/task.h"
// // #include "freertos/event_groups.h"
// // #include "esp_system.h"
// // #include "esp_event.h"
// // #include "esp_netif.h"
// // #include "nvs_flash.h"
// // #include "esp_log.h"

// // /* Core interfaces */
// // #include "service_interfaces.h"
// // #include "event_types.h"
// // #include "command_params.h"

// // /* Services */
// // #include "wifi_service.h"
// // #include "mqtt_service.h"
// // #include "mqtt_topic.h"

// // #define CONFIG_WIFI_SSID "Mathias' Sxx U..."
// // #define CONFIG_WIFI_PASSWORD "1234567890223"
// // #define CONFIG_MQTT_BROKER_URI "mqtt://102.223.8.140:1883"
// // #define CONFIG_MQTT_CLIENT_ID "your_client_id"
// // #define CONFIG_MQTT_USERNAME "mqtt_user"
// // #define CONFIG_MQTT_PASSWORD "ega12345"

// // static const char *TAG = "MAIN";

// // /* Event group for synchronization */
// // static EventGroupHandle_t s_test_events = NULL;
// // static const int WIFI_GOT_IP_BIT = BIT0;

// // /* -------------------------------------------------------------------------
// //  * WiFi event handler (local, signals event group)
// //  * ------------------------------------------------------------------------- */
// // static void wifi_event_handler(system_event_id_t event, void *data, void *ctx)
// // {
// //     if (event == EVENT_WIFI_GOT_IP) {
// //         ESP_LOGI(TAG, "WiFi got IP, signaling");
// //         xEventGroupSetBits(s_test_events, WIFI_GOT_IP_BIT);
// //     }
// // }

// // /* -------------------------------------------------------------------------
// //  * MQTT message handler (prints received messages)
// //  * ------------------------------------------------------------------------- */
// // static void mqtt_message_handler(system_event_id_t event, void *data, void *ctx)
// // {
// //     if (event == EVENT_NETWORK_MESSAGE_RECEIVED) {
// //         system_event_t *ev = (system_event_t*)data;
// //         ESP_LOGI(TAG, "MQTT received: topic=%s, payload=%.*s, qos=%d, retain=%d",
// //                  ev->data.mqtt_message.topic,
// //                  (int)ev->data.mqtt_message.payload_len,
// //                  (const char*)ev->data.mqtt_message.payload,
// //                  ev->data.mqtt_message.qos,
// //                  ev->data.mqtt_message.retain);
// //     }
// // }

// // /* -------------------------------------------------------------------------
// //  * Main application
// //  * ------------------------------------------------------------------------- */
// // void app_main(void)
// // {
// //     esp_err_t ret;

// //     /* 1. Initialize NVS */
// //     ret = nvs_flash_init();
// //     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
// //         ESP_ERROR_CHECK(nvs_flash_erase());
// //         ret = nvs_flash_init();
// //     }
// //     ESP_ERROR_CHECK(ret);

// //     /* 2. Initialize TCP/IP stack and default event loop */
// //     ESP_ERROR_CHECK(esp_netif_init());
// //     ESP_ERROR_CHECK(esp_event_loop_create_default());

// //     /* 3. Create event group for coordination */
// //     s_test_events = xEventGroupCreate();
// //     if (!s_test_events) {
// //         ESP_LOGE(TAG, "Failed to create event group");
// //         return;
// //     }

// //     /* 4. Register local event handlers */
// //     ESP_ERROR_CHECK(core_register_event_handler(EVENT_WIFI_GOT_IP, wifi_event_handler, NULL));
// //     ESP_ERROR_CHECK(core_register_event_handler(EVENT_NETWORK_MESSAGE_RECEIVED, mqtt_message_handler, NULL));

// //     /* 5. Initialize WiFi service */
// //     ESP_ERROR_CHECK(wifi_service_init());
// //     ESP_ERROR_CHECK(wifi_service_register_handlers());
// //     ESP_ERROR_CHECK(wifi_service_start());

// //     /* 6. Initialize MQTT service */
// //     ESP_ERROR_CHECK(mqtt_service_init());
// //     ESP_ERROR_CHECK(mqtt_service_register_handlers());
// //     ESP_ERROR_CHECK(mqtt_service_start());

// //     /* 7. Prepare WiFi credentials (injected via command) */
// //     cmd_connect_wifi_params_t wifi_conn = {
// //         .ssid = CONFIG_WIFI_SSID,          /* Set in menuconfig */
// //         .password = CONFIG_WIFI_PASSWORD,
// //         .auth_mode = 0                      /* open/WPA2 etc., driver will handle */
// //     };
// //     ESP_LOGI(TAG, "Connecting to WiFi...");
// //     ESP_ERROR_CHECK(core_post_command(CMD_CONNECT_WIFI, &wifi_conn));

// //     /* 8. Wait for WiFi to obtain IP */
// //     xEventGroupWaitBits(s_test_events, WIFI_GOT_IP_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
// //     ESP_LOGI(TAG, "WiFi connected, IP obtained");

// //     /* 9. Prepare MQTT broker configuration */
// //     cmd_connect_mqtt_params_t mqtt_conn = {
// //         .broker_uri = CONFIG_MQTT_BROKER_URI,
// //         .client_id = CONFIG_MQTT_CLIENT_ID,
// //         .username = CONFIG_MQTT_USERNAME,
// //         .password = CONFIG_MQTT_PASSWORD,
// //         .keepalive = 120,
// //         .disable_clean_session = false,
// //         .lwt_qos = 0,
// //         .lwt_retain = false,
// //         .lwt_topic = "",
// //         .lwt_message = "",
// //         .min_retry_delay_ms = 2000,
// //         .max_retry_delay_ms = 30000,
// //         .max_retry_attempts = 5
// //     };
// //     ESP_LOGI(TAG, "Connecting to MQTT broker...");
// //     ESP_ERROR_CHECK(core_post_command(CMD_CONNECT_MQTT, &mqtt_conn));

// //     /* 10. Build topic using MAC-based base */
// //     char base_topic[32];
// //     ESP_ERROR_CHECK(mqtt_topic_init(base_topic, sizeof(base_topic)));
// //     ESP_LOGI(TAG, "Base topic: %s", base_topic);

// //     /* Subscribe to devices/<mac>/cmd/test */
// //     char sub_topic[128];
// //     ESP_ERROR_CHECK(mqtt_topic_build(sub_topic, sizeof(sub_topic), "cmd/test"));
// //     cmd_subscribe_mqtt_params_t sub = {
// //         .topic = {0},
// //         .qos = 1
// //     };
// //     strlcpy(sub.topic, sub_topic, sizeof(sub.topic));
// //     ESP_LOGI(TAG, "Subscribing to %s", sub_topic);
// //     ESP_ERROR_CHECK(core_post_command(CMD_SUBSCRIBE_MQTT, &sub));

// //     /* Publish to devices/<mac>/status/online */
// //     char pub_topic[128];
// //     ESP_ERROR_CHECK(mqtt_topic_build(pub_topic, sizeof(pub_topic), "status/online"));
// //     cmd_publish_mqtt_params_t pub = {
// //         .topic = {0},
// //         .payload = "online",
// //         .payload_len = strlen("online"),
// //         .qos = 1,
// //         .retain = true
// //     };
// //     strlcpy(pub.topic, pub_topic, sizeof(pub.topic));
// //     ESP_LOGI(TAG, "Publishing %s", pub_topic);
// //     ESP_ERROR_CHECK(core_post_command(CMD_PUBLISH_MQTT, &pub));

// //     /* 11. Main loop – just keep running, messages printed by handler */
// //     ESP_LOGI(TAG, "Test running. Waiting for MQTT messages...");
// //     while (1) {
// //         vTaskDelay(pdMS_TO_TICKS(1000));
// //     }
// // }













// /**
//  * @file app_main.c
//  * @brief Validation harness for timer, indicator, ultrasonic, and servo services.
//  *
//  * =============================================================================
//  * Steps:
//  *   1. Core initialisation.
//  *   2. Service initialisation via service_manager.
//  *   3. Register handlers.
//  *   4. Start services.
//  *   5. Wait for stabilisation.
//  *   6. Dispatch test commands (indicator, timer, ultrasonic, servo).
//  *   7. Keep main task alive.
//  * =============================================================================
//  */

// #include <stdio.h>
// #include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_log.h"

// #include "command_router.h"
// #include "event_dispatcher.h"
// #include "service_manager.h"
// #include "command_params.h"

// static const char *TAG = "APP_MAIN";

// void app_main(void)
// {
//     ESP_LOGI(TAG, "=== System Validation Harness Starting ===");

//     /* --------------------------------------------------------
//      * 1. Core initialisation
//      * -------------------------------------------------------- */
//     ESP_LOGI(TAG, "Initialising command router");
//     esp_err_t ret = command_router_init();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "command_router_init failed: %d", ret);
//         return;
//     }

//     ESP_LOGI(TAG, "Initialising event dispatcher");
//     ret = event_dispatcher_init();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret);
//         return;
//     }

//     /* --------------------------------------------------------
//      * 2. Service initialisation
//      * -------------------------------------------------------- */
//     ret = service_manager_init_all();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "service_manager_init_all failed: %d", ret);
//         return;
//     }

//     /* --------------------------------------------------------
//      * 3. Register command handlers
//      * -------------------------------------------------------- */
//     ret = service_manager_register_all();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "service_manager_register_all failed: %d", ret);
//         return;
//     }

//     /* --------------------------------------------------------
//      * 4. Start services
//      * -------------------------------------------------------- */
//     ret = service_manager_start_all();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "service_manager_start_all failed: %d", ret);
//         return;
//     }

//     /* --------------------------------------------------------
//      * 5. Allow system to stabilise
//      * -------------------------------------------------------- */
//     ESP_LOGI(TAG, "Waiting 1 second for stabilisation...");
//     vTaskDelay(pdMS_TO_TICKS(1000));

//     /* --------------------------------------------------------
//      * 6. Dispatch test commands
//      * -------------------------------------------------------- */

//     /* ---- Indicator test ---- */
//     cmd_update_indicators_params_t ind_params = {
//         .led_pattern = 2,
//         .buzzer_pattern = 3,
//         .lcd_pattern = 1
//     };
//     ESP_LOGI(TAG, "Dispatching CMD_UPDATE_INDICATORS");
//     command_router_execute(CMD_UPDATE_INDICATORS, &ind_params);

//     /* ---- Periodic timer test (5 sec) ---- */
//     cmd_start_timer_params_t timer_params = { .timeout_ms = 5000 };
//     ESP_LOGI(TAG, "Dispatching CMD_START_PERIODIC_TIMER (5000 ms)");
//     command_router_execute(CMD_START_PERIODIC_TIMER, &timer_params);

//     /* ---- Oneshot timer test (15 sec) ---- */
//     timer_params.timeout_ms = 15000;
//     ESP_LOGI(TAG, "Dispatching CMD_START_INTENT_TIMER (15000 ms)");
//     command_router_execute(CMD_START_INTENT_TIMER, &timer_params);

//     /* ---- Ultrasonic fill level test (every 5 sec) – loop later ---- */

//     /* ---- Servo test sequence ---- */
//     vTaskDelay(pdMS_TO_TICKS(2000));

//     servo_command_data_t servo_cmd = {
//         .servo_id = SERVO_LID,
//         .angle_deg = 0  /* not used for open/close, but keep */
//     };

//     ESP_LOGI(TAG, "Dispatching CMD_OPEN_LID (lid)");
//     command_router_execute(CMD_OPEN_LID, &servo_cmd);

//     vTaskDelay(pdMS_TO_TICKS(5000));   /* wait for motion to complete (smooth) */

//     ESP_LOGI(TAG, "Dispatching CMD_CLOSE_LID (lid)");
//     command_router_execute(CMD_CLOSE_LID, &servo_cmd);

//     vTaskDelay(pdMS_TO_TICKS(5000));

//     /* ---- Continuous ultrasonic fill measurement ---- */
//     int loop = 0;
//     while (1) {
//         vTaskDelay(pdMS_TO_TICKS(5000));
//         ESP_LOGI(TAG, "Dispatching CMD_READ_FILL_LEVEL (attempt %d)", ++loop);
//         command_router_execute(CMD_READ_FILL_LEVEL, NULL);
//     }
// }



















// // // /**
// // //  * @file app_main.c
// // //  * @brief Validation harness for timer, indicator, and ultrasonic services.
// // //  *
// // //  * =============================================================================
// // //  * This file demonstrates correct boot order and direct command injection
// // //  * to verify service behaviour without core FSM involvement.
// // //  *
// // //  * Added ultrasonic test:
// // //  *   - After initialisation, a loop sends CMD_READ_FILL_LEVEL every 5 seconds.
// // //  *   - The ultrasonic service will measure the fill sensor, post EVENT_BIN_LEVEL_UPDATED,
// // //  *     and also log the distance and fill percentage at DEBUG level.
// // //  *   - The intent sensor runs autonomously via its own timer and posts
// // //  *     EVENT_CLOSE_RANGE_DETECTED/LOST when state changes.
// // //  *
// // //  * All commands are sent using command_router_execute().
// // //  * Parameter structures are stack‑allocated and zero‑initialised.
// // //  * =============================================================================
// // //  */

// // // #include <stdio.h>
// // // #include <string.h>
// // // #include "freertos/FreeRTOS.h"
// // // #include "freertos/task.h"
// // // #include "esp_log.h"

// // // /* Core headers */
// // // #include "command_router.h"
// // // #include "event_dispatcher.h"

// // // /* Service manager */
// // // #include "service_manager.h"

// // // /* Command parameter definitions */
// // // #include "command_params.h"

// // // static const char *TAG = "APP_MAIN";

// // // void app_main(void)
// // // {
// // //     ESP_LOGI(TAG, "=== System Validation Harness Starting ===");

// // //     /* --------------------------------------------------------
// // //      * 1. Core initialisation
// // //      * -------------------------------------------------------- */
// // //     ESP_LOGI(TAG, "Initialising command router");
// // //     esp_err_t ret = command_router_init();
// // //     if (ret != ESP_OK) {
// // //         ESP_LOGE(TAG, "command_router_init failed: %d", ret);
// // //         return;
// // //     }

// // //     ESP_LOGI(TAG, "Initialising event dispatcher");
// // //     ret = event_dispatcher_init();
// // //     if (ret != ESP_OK) {
// // //         ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret);
// // //         return;
// // //     }

// // //     /* --------------------------------------------------------
// // //      * 2. Service initialisation (all services)
// // //      * -------------------------------------------------------- */
// // //     ret = service_manager_init_all();
// // //     if (ret != ESP_OK) {
// // //         ESP_LOGE(TAG, "service_manager_init_all failed: %d", ret);
// // //         return;
// // //     }

// // //     /* --------------------------------------------------------
// // //      * 3. Register command handlers
// // //      * -------------------------------------------------------- */
// // //     ret = service_manager_register_all();
// // //     if (ret != ESP_OK) {
// // //         ESP_LOGE(TAG, "service_manager_register_all failed: %d", ret);
// // //         return;
// // //     }

// // //     /* --------------------------------------------------------
// // //      * 4. Start services
// // //      * -------------------------------------------------------- */
// // //     ret = service_manager_start_all();
// // //     if (ret != ESP_OK) {
// // //         ESP_LOGE(TAG, "service_manager_start_all failed: %d", ret);
// // //         return;
// // //     }

// // //     /* --------------------------------------------------------
// // //      * 5. Allow system to stabilise
// // //      * -------------------------------------------------------- */
// // //     ESP_LOGI(TAG, "Waiting 1 second for stabilisation...");
// // //     vTaskDelay(pdMS_TO_TICKS(1000));

// // //     /* --------------------------------------------------------
// // //      * 6. Dispatch test commands (timer & indicator)
// // //      * -------------------------------------------------------- */

// // //     /* ---- Indicator test: LED slow blink, buzzer fast blink, LCD solid ---- */
// // //     cmd_update_indicators_params_t ind_params;
// // //     memset(&ind_params, 0, sizeof(ind_params));
// // //     ind_params.led_pattern = 2;      /* slow blink (500 ms) */
// // //     ind_params.buzzer_pattern = 3;   /* fast blink (200 ms) */
// // //     ind_params.lcd_pattern = 1;      /* solid on */

// // //     ESP_LOGI(TAG, "Dispatching CMD_UPDATE_INDICATORS");
// // //     ret = command_router_execute(CMD_UPDATE_INDICATORS, &ind_params);
// // //     if (ret != ESP_OK) {
// // //         ESP_LOGE(TAG, "CMD_UPDATE_INDICATORS failed: %d", ret);
// // //     }

// // //     /* ---- Periodic timer test: 5000 ms ---- */
// // //     cmd_start_timer_params_t timer_params;
// // //     memset(&timer_params, 0, sizeof(timer_params));
// // //     timer_params.timeout_ms = 5000;

// // //     ESP_LOGI(TAG, "Dispatching CMD_START_PERIODIC_TIMER (5000 ms)");
// // //     ret = command_router_execute(CMD_START_PERIODIC_TIMER, &timer_params);
// // //     if (ret != ESP_OK) {
// // //         ESP_LOGE(TAG, "CMD_START_PERIODIC_TIMER failed: %d", ret);
// // //     }

// // //     /* ---- Oneshot timer test: 15000 ms (using CMD_START_INTENT_TIMER) ---- */
// // //     timer_params.timeout_ms = 15000;
// // //     ESP_LOGI(TAG, "Dispatching CMD_START_INTENT_TIMER (15000 ms)");
// // //     ret = command_router_execute(CMD_START_INTENT_TIMER, &timer_params);
// // //     if (ret != ESP_OK) {
// // //         ESP_LOGE(TAG, "CMD_START_INTENT_TIMER failed: %d", ret);
// // //     }

// // //     /* --------------------------------------------------------
// // //      * 7. Ultrasonic test – periodic fill level measurement
// // //      * -------------------------------------------------------- */
// // //     ESP_LOGI(TAG, "Starting ultrasonic fill level test (every 5s)");
// // //     int loop_count = 0;
// // //     while (1) {
// // //         vTaskDelay(pdMS_TO_TICKS(5000));   /* wait 5 seconds between measurements */

// // //         ESP_LOGI(TAG, "Dispatching CMD_READ_FILL_LEVEL (attempt %d)", ++loop_count);
// // //         ret = command_router_execute(CMD_READ_FILL_LEVEL, NULL);
// // //         if (ret != ESP_OK) {
// // //             ESP_LOGE(TAG, "CMD_READ_FILL_LEVEL failed: %d", ret);
// // //         } else {
// // //             ESP_LOGI(TAG, "CMD_READ_FILL_LEVEL dispatched successfully – check DEBUG logs for distance/percentage");
// // //         }

// // //         /* The intent sensor runs autonomously; its events will appear in logs
// // //          * if DEBUG level is enabled for the ULTRASONIC_SVC tag.
// // //          */
// // //     }

// // //     /* Never reached – infinite loop above */
// // // }