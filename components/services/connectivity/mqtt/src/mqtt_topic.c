#include "mqtt_topic.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>

/* Static storage for base topic (module-level, not global to application) */
static char s_base_topic[32] = {0};  /* Enough for "devices/xxxxxxxxxxxx/" plus null */

/* Maximum MAC address string length (12 hex chars) */
#define MAC_STR_LEN 12

/* Base topic prefix */
#define BASE_PREFIX "devices/"

esp_err_t mqtt_topic_init(char* base_topic_buffer, size_t size)
{
    if (!base_topic_buffer || size < 18) {  /* Minimum: "devices/xxxxxxxxxxxx/" + null = 18 */
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6];
    esp_err_t ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Format MAC as lowercase hex without separators */
    char mac_str[MAC_STR_LEN + 1];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Build base topic: "devices/<mac>/" */
    int len = snprintf(s_base_topic, sizeof(s_base_topic), "%s%s/", BASE_PREFIX, mac_str);
    if (len < 0 || (size_t)len >= sizeof(s_base_topic)) {
        return ESP_FAIL;  /* Should never happen */
    }

    /* Copy to user buffer if provided */
    if (base_topic_buffer) {
        strlcpy(base_topic_buffer, s_base_topic, size);
    }

    return ESP_OK;
}

esp_err_t mqtt_topic_build(char* out, size_t size, const char* subpath)
{
    if (!out || size == 0 || !subpath) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_base_topic[0] == '\0') {
        return ESP_ERR_INVALID_STATE;  /* mqtt_topic_init not called */
    }

    size_t base_len = strlen(s_base_topic);
    size_t sub_len = strlen(subpath);

    /* Check if total length fits (including null terminator) */
    if (base_len + sub_len + 1 > size) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Copy base then subpath */
    memcpy(out, s_base_topic, base_len);
    memcpy(out + base_len, subpath, sub_len + 1);  /* include null terminator */

    return ESP_OK;
}