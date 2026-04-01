#include "web_command_service.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "command_router.h"
#include "mqtt_topic.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "WEB_CMD_SVC";

static esp_err_t handle_process_web_command(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    const web_command_params_t *cmd = (const web_command_params_t*)params;
    const char *topic = cmd->topic;

    /* Build expected device command topic */
    char expected_cmd_topic[128];
    mqtt_topic_build(expected_cmd_topic, sizeof(expected_cmd_topic), "cmd");
    char expected_reg_topic[128];
    mqtt_topic_build(expected_reg_topic, sizeof(expected_reg_topic), "cmd/registration");

    /* Filter only our device command topics */
    if (strcmp(topic, expected_cmd_topic) != 0 && strcmp(topic, expected_reg_topic) != 0) {
        return ESP_OK;   /* ignore other network messages */
    }

    /* Process JSON command */
    cJSON *root = cJSON_ParseWithLength((const char*)cmd->payload, cmd->payload_len);
    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON payload");
        return ESP_FAIL;
    }

    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *action_str = action->valuestring;
    esp_err_t ret = ESP_OK;

    if (strcmp(action_str, "open_lid") == 0) {
        ret = command_router_execute(CMD_OPEN_LID, NULL);
    } else if (strcmp(action_str, "close_lid") == 0) {
        ret = command_router_execute(CMD_CLOSE_LID, NULL);
    } else if (strcmp(action_str, "lock_bin") == 0) {
        ret = command_router_execute(CMD_LOCK_BIN, NULL);
    } else if (strcmp(action_str, "unlock_bin") == 0) {
        ret = command_router_execute(CMD_UNLOCK_BIN, NULL);
    } else if (strcmp(action_str, "reboot") == 0) {
        ret = command_router_execute(CMD_REBOOT_SYSTEM, NULL);
    } else if (strcmp(action_str, "update_config") == 0) {
        cJSON *params = cJSON_GetObjectItem(root, "params");
        if (params) {
            // TODO: parse and save configuration
            ESP_LOGI(TAG, "Config update received");
        }
        ret = ESP_OK;
    } else if (strcmp(action_str, "register") == 0) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"registered\",\"device\":\"smart_bin\"}");
        cmd_publish_mqtt_params_t pub;
        mqtt_topic_build(pub.topic, sizeof(pub.topic), "response");
        strlcpy((char*)pub.payload, response, sizeof(pub.payload));
        pub.payload_len = strlen(response);
        pub.qos = 1;
        pub.retain = false;
        command_router_execute(CMD_PUBLISH_MQTT, &pub);
        ret = ESP_OK;
    } else {
        ESP_LOGW(TAG, "Unknown action: %s", action_str);
        ret = ESP_ERR_NOT_FOUND;
    }

    cJSON_Delete(root);
    return ret;
}

esp_err_t web_command_service_init(void)
{
    ESP_LOGI(TAG, "Web command service initialised");
    return ESP_OK;
}

esp_err_t web_command_service_register_handlers(void)
{
    return service_register_command(CMD_PROCESS_WEB_COMMAND, handle_process_web_command, NULL);
}

esp_err_t web_command_service_start(void)
{
    /* Subscribe to device command topics using MQTT service */
    cmd_subscribe_mqtt_params_t sub;
    sub.qos = 1;

    char topic[128];
    mqtt_topic_build(topic, sizeof(topic), "cmd");
    strlcpy(sub.topic, topic, sizeof(sub.topic));
    command_router_execute(CMD_SUBSCRIBE_MQTT, &sub);

    mqtt_topic_build(topic, sizeof(topic), "cmd/registration");
    strlcpy(sub.topic, topic, sizeof(sub.topic));
    command_router_execute(CMD_SUBSCRIBE_MQTT, &sub);

    ESP_LOGI(TAG, "Web command service started");
    return ESP_OK;
}