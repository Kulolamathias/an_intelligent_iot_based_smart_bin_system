/**
 * @file tests/include/wifi_mqtt_test.h
 * @brief WiFi and MQTT service test – verifies connectivity.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This test runs on top of an already initialised system. It sends WiFi connect
 * commands, waits for connection, then connects to an MQTT broker, subscribes,
 * publishes, and verifies that messages are received.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef WIFI_MQTT_TEST_H
#define WIFI_MQTT_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the WiFi + MQTT test.
 *
 * Assumes:
 * - command_router is initialised and locked
 * - event_dispatcher is started
 * - wifi_service and mqtt_service are registered and started
 *
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t wifi_mqtt_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MQTT_TEST_H */