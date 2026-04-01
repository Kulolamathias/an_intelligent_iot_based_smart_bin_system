/**
 * @file bin_network_service.c
 * @brief Implementation of the bin network service.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Receives commands via a single command handler and places them into a
 *   private queue for processing in its own task.
 * - Maintains a static peer registry (max 16 entries).
 * - Uses a FreeRTOS software timer for periodic heartbeat (every 10 s).
 * - When local bin becomes full, selects nearest available peer and emits
 *   EVENT_REDIRECT_TO_BIN.
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#include "bin_network_service.h"
#include "mqtt_topic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include "service_interfaces.h"
#include "event_types.h"
#include "command_params.h"
#include "cJSON.h"
#include <string.h>
#include <math.h>

static const char* TAG = "BIN_NET_SVC";

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
#define MAX_PEERS               16
#define PEER_EXPIRY_SEC         30
#define HEARTBEAT_PERIOD_MS     10000   /* 10 seconds */
#define BIN_ID_STR_LEN           13      /* 12 hex chars + null */
#define JSON_BUFFER_SIZE        512

/* -------------------------------------------------------------------------
 * Peer status enumeration
 * ------------------------------------------------------------------------- */
typedef enum {
    BIN_STATUS_AVAILABLE,
    BIN_STATUS_FULL,
    BIN_STATUS_FAULT
} bin_status_t;

/* -------------------------------------------------------------------------
 * Peer registry entry
 * ------------------------------------------------------------------------- */
typedef struct {
    char id[BIN_ID_STR_LEN];            /**< Bin ID (MAC address) */
    float latitude;                       /**< Latitude (degrees) */
    float longitude;                      /**< Longitude (degrees) */
    uint8_t fill_percent;                 /**< 0–100% */
    uint8_t capacity;                     /**< Total capacity (litres or units) */
    bin_status_t status;                  /**< AVAILABLE, FULL, FAULT */
    uint32_t last_seen_timestamp;         /**< Unix epoch (seconds) */
    bool active;                          /**< true if heartbeat received within expiry */
} peer_bin_t;

/* -------------------------------------------------------------------------
 * Internal event types (for the service's own queue)
 * ------------------------------------------------------------------------- */
typedef enum {
    INT_EVT_MQTT_CONNECTED,
    INT_EVT_NETWORK_MESSAGE,
    INT_EVT_LEVEL_UPDATE,
    INT_EVT_HEARTBEAT
} internal_event_id_t;

typedef struct {
    internal_event_id_t id;
    union {
        cmd_bin_net_network_msg_t network_msg;
        cmd_bin_net_level_update_t level_update;
    } data;
} internal_event_t;

/* -------------------------------------------------------------------------
 * Service context (static)
 * ------------------------------------------------------------------------- */
typedef struct {
    TaskHandle_t task;                          /**< Service task handle */
    QueueHandle_t queue;                        /**< Internal event queue */
    esp_timer_handle_t heartbeat_timer;         /**< Periodic heartbeat timer */
    peer_bin_t peers[MAX_PEERS];                /**< Static peer registry */
    uint32_t peer_count;                        /**< Current number of valid peers */
    bool mqtt_connected;                        /**< True if MQTT is up */
    char own_id[BIN_ID_STR_LEN];                /**< Own bin ID (MAC) */
    float own_latitude;                         /**< Last known latitude */
    float own_longitude;                        /**< Last known longitude */
    uint8_t own_fill_percent;                   /**< Current fill level */
    uint8_t own_capacity;                       /**< Bin capacity (config) */
} bin_network_ctx_t;

static bin_network_ctx_t s_ctx = {0};

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
static void bin_network_task(void *pvParameters);
static void process_internal_event(const internal_event_t *ev);
static void heartbeat_timer_callback(void *arg);
static void publish_heartbeat(void);
static void publish_state(void);
static void publish_cloud_heartbeat(void);
static void subscribe_topics(void);
static void publish_device_online(void);
static void handle_peer_announce(const char *payload, size_t len);
static void handle_peer_state(const char *topic, const char *payload, size_t len);
static void handle_lwt_offline(const char *topic);
static void update_peer_activity(const char *id);
static void expire_peers(void);
static peer_bin_t* find_peer(const char *id);
static peer_bin_t* add_peer(const char *id);
static void select_nearest_bin(void);
static float haversine(float lat1, float lon1, float lat2, float lon2);
static uint32_t get_current_timestamp(void);

/* -------------------------------------------------------------------------
 * Command handler (registered with command router)
 * ------------------------------------------------------------------------- */
static esp_err_t bin_net_cmd_handler(void *context, void *params)
{
    (void)context;

    system_command_id_t cmd_id = *(system_command_id_t*)params; // but params is the parameter struct, not the cmd id.
    // We need to pass cmd_id in the command router call; the handler receives the parameters.
    // The handler is registered per command, so we need separate handlers or a single handler that receives the command ID via context.
    // Simpler: register separate handlers as in timer_service. Let's do that.
    // We'll implement separate static handlers for each command, each sending an internal event.
    // That's cleaner and matches the pattern.
    // So we'll create three handlers: handle_mqtt_connected, handle_network_message, handle_level_update.
    // They will each post an internal_event_t to the queue.
    return ESP_OK; // placeholder, will be replaced.
}

// To avoid confusion, we'll implement separate handlers as in timer_service.
static esp_err_t handle_mqtt_connected(void *context, void *params)
{
    (void)context;
    (void)params;

    internal_event_t ev = {
        .id = INT_EVT_MQTT_CONNECTED
    };
    if (xQueueSend(s_ctx.queue, &ev, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t handle_network_message(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    const cmd_bin_net_network_msg_t *p = (const cmd_bin_net_network_msg_t*)params;
    internal_event_t ev;
    ev.id = INT_EVT_NETWORK_MESSAGE;
    memcpy(&ev.data.network_msg, p, sizeof(cmd_bin_net_network_msg_t));
    if (xQueueSend(s_ctx.queue, &ev, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t handle_level_update(void *context, void *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    const cmd_bin_net_level_update_t *p = (const cmd_bin_net_level_update_t*)params;
    internal_event_t ev;
    ev.id = INT_EVT_LEVEL_UPDATE;
    ev.data.level_update.fill_level_percent = p->fill_level_percent;
    if (xQueueSend(s_ctx.queue, &ev, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void publish_device_online(void)
{
    char topic[128];
    mqtt_topic_build(topic, sizeof(topic), "status/online");
    const char *payload = "online";
    cmd_publish_mqtt_params_t pub;
    strlcpy(pub.topic, topic, sizeof(pub.topic));
    strlcpy((char*)pub.payload, payload, sizeof(pub.payload));
    pub.payload_len = strlen(payload);
    pub.qos = 1;
    pub.retain = true;
    command_router_execute(CMD_PUBLISH_MQTT, &pub);
    ESP_LOGI(TAG, "Published online status to %s", topic);
}

/* -------------------------------------------------------------------------
 * Public API (service manager lifecycle)
 * ------------------------------------------------------------------------- */

esp_err_t bin_network_service_init(void)
{
    if (s_ctx.queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Create internal event queue */
    s_ctx.queue = xQueueCreate(10, sizeof(internal_event_t));
    if (!s_ctx.queue) {
        return ESP_ERR_NO_MEM;
    }

    /* Create heartbeat timer (FreeRTOS software timer) */
    esp_timer_create_args_t timer_args = {
        .callback = heartbeat_timer_callback,
        .arg = NULL,
        .name = "bin_net_hb"
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_ctx.heartbeat_timer);
    if (ret != ESP_OK) {
        // handle error
        return ESP_ERR_NO_MEM;
    }

    /* Retrieve own MAC address for bin ID */
    uint8_t mac[6];
    ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        return ret;
    }
    snprintf(s_ctx.own_id, sizeof(s_ctx.own_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Initialise registry */
    memset(s_ctx.peers, 0, sizeof(s_ctx.peers));
    s_ctx.peer_count = 0;
    s_ctx.mqtt_connected = false;

    /* Create service task */
    BaseType_t ret_task = xTaskCreate(bin_network_task, "bin_net", 4096, NULL, 5, &s_ctx.task);
    if (ret_task != pdPASS) {
        esp_timer_stop(s_ctx.heartbeat_timer);
        esp_timer_delete(s_ctx.heartbeat_timer);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Bin network service initialised, own ID: %s", s_ctx.own_id);
    return ESP_OK;
}

esp_err_t bin_network_service_register_commands(void)
{
    esp_err_t ret;

    ret = service_register_command(CMD_BIN_NET_NOTIFY_MQTT_CONNECTED,
                                   handle_mqtt_connected, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_BIN_NET_NOTIFY_NETWORK_MESSAGE,
                                   handle_network_message, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_BIN_NET_NOTIFY_LEVEL_UPDATE,
                                   handle_level_update, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Bin network command handlers registered");
    return ESP_OK;
}

esp_err_t bin_network_service_start(void)
{
    esp_timer_start_periodic(s_ctx.heartbeat_timer, HEARTBEAT_PERIOD_MS * 1000);
    ESP_LOGI(TAG, "Bin network service started");
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Service task – processes internal events
 * ------------------------------------------------------------------------- */
static void bin_network_task(void *pvParameters)
{
    (void)pvParameters;
    internal_event_t ev;

    while (1) {
        if (xQueueReceive(s_ctx.queue, &ev, portMAX_DELAY) == pdTRUE) {
            process_internal_event(&ev);
        }
    }
}

/* -------------------------------------------------------------------------
 * Process internal events
 * ------------------------------------------------------------------------- */
static void process_internal_event(const internal_event_t *ev)
{
    switch (ev->id) {
        case INT_EVT_MQTT_CONNECTED:
            s_ctx.mqtt_connected = true;
            subscribe_topics();
            publish_heartbeat();
            publish_state();
            publish_cloud_heartbeat();
            publish_device_online();
            break;

        case INT_EVT_NETWORK_MESSAGE: {
            const cmd_bin_net_network_msg_t *msg = &ev->data.network_msg;
            const char *topic = msg->topic;
            const char *payload = (const char*)msg->payload;
            size_t len = msg->payload_len;

            if (strcmp(topic, "smartbin/discovery/announce") == 0) {
                handle_peer_announce(payload, len);
            } else if (strncmp(topic, "smartbin/bin/", 13) == 0) {
                const char *rest = topic + 13;
                const char *slash = strchr(rest, '/');
                if (slash) {
                    size_t id_len = slash - rest;
                    if (id_len < BIN_ID_STR_LEN) {
                        char id[BIN_ID_STR_LEN];
                        memcpy(id, rest, id_len);
                        id[id_len] = '\0';
                        if (strcmp(slash + 1, "state") == 0) {
                            handle_peer_state(id, payload, len);
                        } else if (strcmp(slash + 1, "lwt") == 0) {
                            if (strncmp(payload, "OFFLINE", len) == 0) {
                                handle_lwt_offline(id);
                            }
                        }
                    }
                }
            }
            break;
        }

        case INT_EVT_LEVEL_UPDATE:
            s_ctx.own_fill_percent = ev->data.level_update.fill_level_percent;
            if (s_ctx.own_fill_percent >= 100) {
                select_nearest_bin();
            }
            break;

        case INT_EVT_HEARTBEAT:
            if (!s_ctx.mqtt_connected) break;
            expire_peers();
            publish_heartbeat();
            publish_state();
            publish_cloud_heartbeat();
            break;

        default:
            break;
    }
}

/* -------------------------------------------------------------------------
 * Timer callback (heartbeat)
 * ------------------------------------------------------------------------- */
static void heartbeat_timer_callback(void *arg)
{
    (void)arg;
    internal_event_t ev = { .id = INT_EVT_HEARTBEAT };
    xQueueSend(s_ctx.queue, &ev, 0);
}

/* -------------------------------------------------------------------------
 * Subscribe to MQTT topics (using command router)
 * ------------------------------------------------------------------------- */
static void subscribe_topics(void)
{
    cmd_subscribe_mqtt_params_t sub;

    strlcpy(sub.topic, "smartbin/discovery/announce", sizeof(sub.topic));
    sub.qos = 1;
    command_router_execute(CMD_SUBSCRIBE_MQTT, &sub);

    strlcpy(sub.topic, "smartbin/bin/+/state", sizeof(sub.topic));
    command_router_execute(CMD_SUBSCRIBE_MQTT, &sub);

    strlcpy(sub.topic, "smartbin/bin/+/lwt", sizeof(sub.topic));
    command_router_execute(CMD_SUBSCRIBE_MQTT, &sub);

    ESP_LOGI(TAG, "Subscribed to discovery and peer topics");
}

/* -------------------------------------------------------------------------
 * Publish discovery announce (QoS 1, not retained)
 * ------------------------------------------------------------------------- */
static void publish_heartbeat(void)
{
    char json[JSON_BUFFER_SIZE];
    uint32_t now = get_current_timestamp();

    snprintf(json, sizeof(json),
             "{"
             "\"id\":\"%s\","
             "\"lat\":%.6f,"
             "\"lon\":%.6f,"
             "\"fill\":%u,"
             "\"capacity\":%u,"
             "\"status\":\"%s\","
             "\"timestamp\":%lu"
             "}",
             s_ctx.own_id,
             s_ctx.own_latitude,
             s_ctx.own_longitude,
             s_ctx.own_fill_percent,
             s_ctx.own_capacity,
             s_ctx.own_fill_percent >= 100 ? "FULL" : "AVAILABLE",
             (unsigned long)now);

    cmd_publish_mqtt_params_t pub;
    strlcpy(pub.topic, "smartbin/discovery/announce", sizeof(pub.topic));
    strlcpy((char*)pub.payload, json, sizeof(pub.payload));
    pub.payload_len = strlen(json);
    pub.qos = 1;
    pub.retain = false;
    command_router_execute(CMD_PUBLISH_MQTT, &pub);
}

/* -------------------------------------------------------------------------
 * Publish detailed state (retained = false)
 * ------------------------------------------------------------------------- */
static void publish_state(void)
{
    char json[JSON_BUFFER_SIZE];
    uint32_t now = get_current_timestamp();

    snprintf(json, sizeof(json),
             "{"
             "\"fill\":%u,"
             "\"capacity\":%u,"
             "\"lat\":%.6f,"
             "\"lon\":%.6f,"
             "\"timestamp\":%lu"
             "}",
             s_ctx.own_fill_percent,
             s_ctx.own_capacity,
             s_ctx.own_latitude,
             s_ctx.own_longitude,
             (unsigned long)now);

    char topic[64];
    snprintf(topic, sizeof(topic), "smartbin/bin/%s/state", s_ctx.own_id);
    cmd_publish_mqtt_params_t pub;
    strlcpy(pub.topic, topic, sizeof(pub.topic));
    strlcpy((char*)pub.payload, json, sizeof(pub.payload));
    pub.payload_len = strlen(json);
    pub.qos = 1;
    pub.retain = false;
    command_router_execute(CMD_PUBLISH_MQTT, &pub);
}

/* -------------------------------------------------------------------------
 * Publish cloud heartbeat (separate namespace)
 * ------------------------------------------------------------------------- */
static void publish_cloud_heartbeat(void)
{
    char json[JSON_BUFFER_SIZE];
    uint32_t now = get_current_timestamp();

    snprintf(json, sizeof(json),
             "{"
             "\"fill\":%u,"
             "\"lat\":%.6f,"
             "\"lon\":%.6f,"
             "\"timestamp\":%lu"
             "}",
             s_ctx.own_fill_percent,
             s_ctx.own_latitude,
             s_ctx.own_longitude,
             (unsigned long)now);

    char topic[64];
    snprintf(topic, sizeof(topic), "smartbin/cloud/bin/%s/heartbeat", s_ctx.own_id);
    cmd_publish_mqtt_params_t pub;
    strlcpy(pub.topic, topic, sizeof(pub.topic));
    strlcpy((char*)pub.payload, json, sizeof(pub.payload));
    pub.payload_len = strlen(json);
    pub.qos = 1;
    pub.retain = false;
    command_router_execute(CMD_PUBLISH_MQTT, &pub);
}

/* -------------------------------------------------------------------------
 * Parse incoming peer announcement
 * ------------------------------------------------------------------------- */
static void handle_peer_announce(const char *payload, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root) return;

    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "id"));
    if (!id) { cJSON_Delete(root); return; }

    peer_bin_t *peer = find_peer(id);
    if (!peer) {
        peer = add_peer(id);
        if (!peer) { cJSON_Delete(root); return; }
    }

    /* Update fields */
    cJSON *item;
    item = cJSON_GetObjectItem(root, "lat");
    if (item) peer->latitude = (float)item->valuedouble;
    item = cJSON_GetObjectItem(root, "lon");
    if (item) peer->longitude = (float)item->valuedouble;
    item = cJSON_GetObjectItem(root, "fill");
    if (item) peer->fill_percent = (uint8_t)item->valueint;
    item = cJSON_GetObjectItem(root, "capacity");
    if (item) peer->capacity = (uint8_t)item->valueint;
    item = cJSON_GetObjectItem(root, "status");
    if (item) {
        const char *status = item->valuestring;
        if (strcmp(status, "AVAILABLE") == 0) peer->status = BIN_STATUS_AVAILABLE;
        else if (strcmp(status, "FULL") == 0) peer->status = BIN_STATUS_FULL;
        else if (strcmp(status, "FAULT") == 0) peer->status = BIN_STATUS_FAULT;
    }
    item = cJSON_GetObjectItem(root, "timestamp");
    if (item) peer->last_seen_timestamp = (uint32_t)item->valueint;

    peer->active = true;

    cJSON_Delete(root);
}

/* -------------------------------------------------------------------------
 * Parse incoming detailed state (optional)
 * ------------------------------------------------------------------------- */
static void handle_peer_state(const char *id, const char *payload, size_t len)
{
    peer_bin_t *peer = find_peer(id);
    if (!peer) return;   /* Ignore if we haven't seen announcement yet */

    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root) return;

    cJSON *item;
    item = cJSON_GetObjectItem(root, "fill");
    if (item) peer->fill_percent = (uint8_t)item->valueint;
    item = cJSON_GetObjectItem(root, "capacity");
    if (item) peer->capacity = (uint8_t)item->valueint;
    item = cJSON_GetObjectItem(root, "lat");
    if (item) peer->latitude = (float)item->valuedouble;
    item = cJSON_GetObjectItem(root, "lon");
    if (item) peer->longitude = (float)item->valuedouble;
    item = cJSON_GetObjectItem(root, "timestamp");
    if (item) peer->last_seen_timestamp = (uint32_t)item->valueint;

    peer->active = true;

    cJSON_Delete(root);
}

/* -------------------------------------------------------------------------
 * Handle LWT offline message
 * ------------------------------------------------------------------------- */
static void handle_lwt_offline(const char *id)
{
    peer_bin_t *peer = find_peer(id);
    if (peer) {
        peer->active = false;
        ESP_LOGI(TAG, "Peer %s went offline", id);
    }
}

/* -------------------------------------------------------------------------
 * Expire peers older than PEER_EXPIRY_SEC
 * ------------------------------------------------------------------------- */
static void expire_peers(void)
{
    uint32_t now = get_current_timestamp();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (s_ctx.peers[i].active &&
            (now - s_ctx.peers[i].last_seen_timestamp > PEER_EXPIRY_SEC)) {
            s_ctx.peers[i].active = false;
            ESP_LOGD(TAG, "Peer %s expired", s_ctx.peers[i].id);
        }
    }
}

/* -------------------------------------------------------------------------
 * Peer registry helpers
 * ------------------------------------------------------------------------- */
static peer_bin_t* find_peer(const char *id)
{
    for (int i = 0; i < MAX_PEERS; i++) {
        if (s_ctx.peers[i].id[0] != '\0' && strcmp(s_ctx.peers[i].id, id) == 0) {
            return &s_ctx.peers[i];
        }
    }
    return NULL;
}

static peer_bin_t* add_peer(const char *id)
{
    /* First, look for an empty slot */
    for (int i = 0; i < MAX_PEERS; i++) {
        if (s_ctx.peers[i].id[0] == '\0') {
            strlcpy(s_ctx.peers[i].id, id, sizeof(s_ctx.peers[i].id));
            s_ctx.peer_count++;
            return &s_ctx.peers[i];
        }
    }
    /* No free slot – optionally overwrite oldest inactive? For simplicity, ignore. */
    ESP_LOGW(TAG, "Peer registry full, cannot add %s", id);
    return NULL;
}

/* -------------------------------------------------------------------------
 * Nearest bin selection (Haversine formula)
 * ------------------------------------------------------------------------- */
static void select_nearest_bin(void)
{
    if (s_ctx.peer_count == 0) {
        ESP_LOGI(TAG, "No peers available, cannot redirect");
        return;
    }

    float best_dist = INFINITY;
    peer_bin_t *best = NULL;

    for (int i = 0; i < MAX_PEERS; i++) {
        if (s_ctx.peers[i].active && s_ctx.peers[i].status == BIN_STATUS_AVAILABLE) {
            float dist = haversine(s_ctx.own_latitude, s_ctx.own_longitude,
                                    s_ctx.peers[i].latitude, s_ctx.peers[i].longitude);
            if (dist < best_dist) {
                best_dist = dist;
                best = &s_ctx.peers[i];
            }
        }
    }

    if (best) {
        ESP_LOGI(TAG, "Nearest available bin: %s at %.2f m", best->id, best_dist);
        /* Emit redirect event */
        redirect_info_t info;
        strlcpy(info.peer_id, best->id, sizeof(info.peer_id));
        info.latitude = best->latitude;
        info.longitude = best->longitude;
        info.distance = best_dist;
        system_event_t ev = {
            .id = EVENT_REDIRECT_TO_BIN,
            .data = { .redirect_info = info }
        };
        service_post_event(&ev);
    } else {
        ESP_LOGI(TAG, "No available peers found");
    }
}

/* -------------------------------------------------------------------------
 * Haversine distance (meters)
 * ------------------------------------------------------------------------- */
static float haversine(float lat1, float lon1, float lat2, float lon2)
{
    float R = 6371000.0; /* metres */
    float phi1 = lat1 * M_PI / 180.0;
    float phi2 = lat2 * M_PI / 180.0;
    float dphi = (lat2 - lat1) * M_PI / 180.0;
    float dlambda = (lon2 - lon1) * M_PI / 180.0;

    float a = sinf(dphi / 2.0) * sinf(dphi / 2.0) +
              cosf(phi1) * cosf(phi2) *
              sinf(dlambda / 2.0) * sinf(dlambda / 2.0);
    float c = 2.0 * atan2f(sqrtf(a), sqrtf(1.0 - a));
    return R * c;
}

/* -------------------------------------------------------------------------
 * Get current timestamp (seconds since boot) – for simplicity.
 * In a real system, use RTC or GPS time.
 * ------------------------------------------------------------------------- */
static uint32_t get_current_timestamp(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
}