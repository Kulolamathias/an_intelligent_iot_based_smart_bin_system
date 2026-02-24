#ifndef MQTT_TOPIC_H
#define MQTT_TOPIC_H

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the topic module and generate the base topic.
 *
 * The base topic format is "devices/<mac_address>/" where <mac_address>
 * is the device's MAC address in lowercase hex without separators.
 *
 * @param[out] base_topic_buffer Buffer to receive the base topic string.
 * @param size Size of the buffer (must be at least 18 bytes, typically 32).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if buffer too small,
 *         or other error from MAC retrieval.
 */
esp_err_t mqtt_topic_init(char* base_topic_buffer, size_t size);

/**
 * @brief Build a full topic by appending a subpath to the base topic.
 *
 * The subpath is appended directly after the base topic. No separator
 * is added – the base topic already ends with '/'.
 *
 * Example:
 *   base: "devices/a1b2c3d4e5f6/"
 *   subpath: "cmd/led"
 *   result: "devices/a1b2c3d4e5f6/cmd/led"
 *
 * @param[out] out Buffer to receive the full topic (null-terminated).
 * @param size Size of the output buffer.
 * @param subpath Subpath string (must be null-terminated).
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE if buffer too small,
 *         ESP_ERR_INVALID_STATE if mqtt_topic_init not called first.
 */
esp_err_t mqtt_topic_build(char* out, size_t size, const char* subpath);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TOPIC_H */