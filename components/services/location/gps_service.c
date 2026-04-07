/**
 * @file components/services/sensing/gps_service/gps_service.c
 * @brief GPS Service – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Runs a dedicated task that polls the GPS driver every 500 ms.
 * - When a valid fix is obtained, posts EVENT_GPS_FIX_UPDATED with raw data.
 * - Enriches coordinates with a human‑readable name via a local mapping table.
 * - The mapping table is initially static; later can be extended with NVS.
 * =============================================================================
 */

#include "gps_service.h"
#include "gps_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <math.h>

static const char *TAG = "GPS_SVC";

/* ============================================================
 * Configuration
 * ============================================================ */
#define GPS_POLL_INTERVAL_MS       500     /* 0.5 second between reads */
#define GPS_TASK_STACK_SIZE        4096
#define GPS_TASK_PRIORITY          5
#define MAX_KNOWN_LOCATIONS        32      /* Maximum entries in mapping table */
#define GPS_DEFAULT_RADIUS_METERS  50      /* Default radius for location matching */

#define GPS_TX_PIN 17
#define GPS_RX_PIN 16
#define     GPS_UART_NUM UART_NUM_2

/* ============================================================
 * Internal command types for service queue
 * ============================================================ */
typedef enum {
    GPS_CMD_START,          /* Start GPS acquisition */
    GPS_CMD_STOP,           /* Stop GPS acquisition */
    GPS_CMD_GET_FIX,        /* Get last fix immediately */
    GPS_CMD_ADD_LOCATION,   /* Add/update known location */
    GPS_CMD_SET_NAME        /* Set name for current location */
} gps_internal_cmd_t;

typedef struct {
    gps_internal_cmd_t cmd;
    union {
        struct {
            double latitude;
            double longitude;
            uint16_t radius_meters;
            char name[64];
        } add_location;
        struct {
            char name[64];
        } set_name;
    } data;
} gps_msg_t;

/* ============================================================
 * Known location entry
 * ============================================================ */
typedef struct {
    double latitude;            /* center latitude */
    double longitude;           /* center longitude */
    uint16_t radius_meters;     /* matching radius (meters) */
    char name[64];              /* human‑readable name */
    bool used;                  /* entry in use */
} known_location_t;

/* ============================================================
 * Service context
 * ============================================================ */
typedef struct {
    TaskHandle_t task;                  /* Service task handle */
    QueueHandle_t queue;                /* Command queue */
    gps_handle_t driver;                /* GPS driver handle */
    gps_data_t last_data;               /* Most recent fix */
    bool fix_valid;                     /* Current fix status */
    bool running;                       /* True if acquisition active */
    known_location_t known_locations[MAX_KNOWN_LOCATIONS];
    uint32_t known_count;
} gps_ctx_t;

static gps_ctx_t s_ctx = { { {0} } };

/* ============================================================
 * Local static known location table (pre‑populated examples)
 * ============================================================ */
static const known_location_t s_default_locations[] = {
    { -6.7924, 39.2083, 100, "Dar es Salaam", true },    // Example
    { -6.1639, 35.7516, 100, "Dodoma", true },
    { -3.3667, 36.6833, 100, "Arusha", true },
    /* Add your own known locations here */
};
#define DEFAULT_LOCATION_COUNT (sizeof(s_default_locations) / sizeof(known_location_t))

/* ============================================================
 * Helper: haversine distance (meters)
 * ============================================================ */
static float haversine_meters(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0; /* metres */
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dphi = (lat2 - lat1) * M_PI / 180.0;
    double dlambda = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dphi/2) * sin(dphi/2) +
               cos(phi1) * cos(phi2) *
               sin(dlambda/2) * sin(dlambda/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    return (float)(R * c);
}

/* ============================================================
 * Enrich location with name from known table
 * ============================================================ */
static const char* find_location_name(double lat, double lon)
{
    float best_dist = GPS_DEFAULT_RADIUS_METERS;
    const char *best_name = NULL;

    for (int i = 0; i < MAX_KNOWN_LOCATIONS; i++) {
        if (!s_ctx.known_locations[i].used) continue;
        float dist = haversine_meters(lat, lon,
                                       s_ctx.known_locations[i].latitude,
                                       s_ctx.known_locations[i].longitude);
        if (dist < best_dist) {
            best_dist = dist;
            best_name = s_ctx.known_locations[i].name;
        }
    }
    return best_name;
}

/* ============================================================
 * Post GPS fix event
 * ============================================================ */
static void post_gps_fix(void)
{
    system_event_t ev = {
        .id = EVENT_GPS_FIX_UPDATED,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    ev.data.gps_fix.valid = s_ctx.fix_valid;
    ev.data.gps_fix.latitude = s_ctx.last_data.latitude;
    ev.data.gps_fix.longitude = s_ctx.last_data.longitude;
    ev.data.gps_fix.altitude_m = s_ctx.last_data.altitude_m;
    ev.data.gps_fix.speed_kmh = s_ctx.last_data.speed_kmh;
    ev.data.gps_fix.satellites = s_ctx.last_data.satellites;
    ev.data.gps_fix.hdop = s_ctx.last_data.hdop;
    ev.data.gps_fix.timestamp_ms = s_ctx.last_data.timestamp_ms;
    service_post_event(&ev);
    ESP_LOGD(TAG, "GPS fix posted: %.6f, %.6f", ev.data.gps_fix.latitude, ev.data.gps_fix.longitude);
}

/* ============================================================
 * Post GPS fix lost event
 * ============================================================ */
static void post_gps_fix_lost(void)
{
    system_event_t ev = {
        .id = EVENT_GPS_FIX_LOST,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { { {0} } }
    };
    service_post_event(&ev);
    ESP_LOGD(TAG, "GPS fix lost");
}

/* ============================================================
 * Update GPS data (called periodically)
 * ============================================================ */
static void update_gps_data(void)
{
    if (!s_ctx.running) return;

    /* Read from driver */
    esp_err_t ret = gps_driver_update(s_ctx.driver);
    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "GPS driver update failed: %d", ret);
        return;
    }

    /* Get latest data */
    gps_data_t new_data;
    ret = gps_driver_get_data(s_ctx.driver, &new_data);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get GPS data: %d", ret);
        return;
    }

    /* Detect change in fix status */
    bool new_fix = new_data.fix_valid;
    if (new_fix != s_ctx.fix_valid) {
        if (new_fix) {
            /* Fix acquired */
            s_ctx.last_data = new_data;
            s_ctx.fix_valid = true;
            post_gps_fix();
        } else {
            /* Fix lost */
            s_ctx.fix_valid = false;
            post_gps_fix_lost();
        }
    } else if (new_fix) {
        /* Fix already valid, check if data changed significantly */
        float delta = haversine_meters(s_ctx.last_data.latitude, s_ctx.last_data.longitude,
                                        new_data.latitude, new_data.longitude);
        if (delta > 1.0f || fabs(s_ctx.last_data.altitude_m - new_data.altitude_m) > 1.0f) {
            s_ctx.last_data = new_data;
            post_gps_fix();
        }
    }
}

/* ============================================================
 * Service task
 * ============================================================ */
static void gps_service_task(void *arg)
{
    (void)arg;
    gps_msg_t msg;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        /* Process any commands from the queue (non‑blocking) */
        while (xQueueReceive(s_ctx.queue, &msg, 0) == pdTRUE) {
            switch (msg.cmd) {
                case GPS_CMD_START:
                    if (!s_ctx.running) {
                        s_ctx.running = true;
                        gps_driver_start(s_ctx.driver);
                        ESP_LOGI(TAG, "GPS acquisition started");
                    }
                    break;
                case GPS_CMD_STOP:
                    if (s_ctx.running) {
                        s_ctx.running = false;
                        gps_driver_stop(s_ctx.driver);
                        ESP_LOGI(TAG, "GPS acquisition stopped");
                    }
                    break;
                case GPS_CMD_GET_FIX:
                    if (s_ctx.fix_valid) {
                        post_gps_fix();
                    } else {
                        ESP_LOGD(TAG, "No GPS fix available");
                    }
                    break;
                case GPS_CMD_ADD_LOCATION: {
                    const char *name = msg.data.add_location.name;
                    double lat = msg.data.add_location.latitude;
                    double lon = msg.data.add_location.longitude;
                    uint16_t radius = msg.data.add_location.radius_meters;
                    /* Find an empty slot or update existing */
                    int idx = -1;
                    for (int i = 0; i < MAX_KNOWN_LOCATIONS; i++) {
                        if (!s_ctx.known_locations[i].used) {
                            idx = i;
                            break;
                        }
                        /* If name matches, update */
                        if (s_ctx.known_locations[i].used &&
                            strcmp(s_ctx.known_locations[i].name, name) == 0) {
                            idx = i;
                            break;
                        }
                    }
                    if (idx >= 0) {
                        s_ctx.known_locations[idx].latitude = lat;
                        s_ctx.known_locations[idx].longitude = lon;
                        s_ctx.known_locations[idx].radius_meters = radius;
                        strlcpy(s_ctx.known_locations[idx].name, name,
                                sizeof(s_ctx.known_locations[idx].name));
                        s_ctx.known_locations[idx].used = true;
                        ESP_LOGI(TAG, "Added location: %s (%.6f, %.6f)", name, lat, lon);
                    } else {
                        ESP_LOGW(TAG, "Known location table full");
                    }
                    break;
                }
                case GPS_CMD_SET_NAME: {
                    if (s_ctx.fix_valid) {
                        const char *name = msg.data.set_name.name;
                        /* Add the current location with the given name */
                        gps_msg_t add_msg = {
                            .cmd = GPS_CMD_ADD_LOCATION,
                            .data.add_location = {
                                .latitude = s_ctx.last_data.latitude,
                                .longitude = s_ctx.last_data.longitude,
                                .radius_meters = GPS_DEFAULT_RADIUS_METERS
                            }
                        };
                        strlcpy(add_msg.data.add_location.name, name,
                                sizeof(add_msg.data.add_location.name));
                        xQueueSend(s_ctx.queue, &add_msg, 0);
                        ESP_LOGI(TAG, "Set name '%s' for current location", name);
                    } else {
                        ESP_LOGW(TAG, "No GPS fix to set name");
                    }
                    break;
                }
                default:
                    break;
            }
        }

        /* Update GPS data if running */
        if (s_ctx.running) {
            update_gps_data();
        }

        /* Wait for next polling interval */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(GPS_POLL_INTERVAL_MS));
    }
}

/* ============================================================
 * Command handlers (registered with command router)
 * ============================================================ */

static esp_err_t cmd_gps_start(void *context, const command_param_union_t *params)
{
    (void)context; (void)params;
    gps_msg_t msg = { .cmd = GPS_CMD_START };
    return (xQueueSend(s_ctx.queue, &msg, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_gps_stop(void *context, const command_param_union_t *params)
{
    (void)context; (void)params;
    gps_msg_t msg = { .cmd = GPS_CMD_STOP };
    return (xQueueSend(s_ctx.queue, &msg, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_gps_get_last_fix(void *context, const command_param_union_t *params)
{
    (void)context; (void)params;
    gps_msg_t msg = { .cmd = GPS_CMD_GET_FIX };
    return (xQueueSend(s_ctx.queue, &msg, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_gps_add_known_location(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const gps_add_location_params_t *p = &params->gps_add_location;
    gps_msg_t msg = {
        .cmd = GPS_CMD_ADD_LOCATION,
        .data.add_location = {
            .latitude = p->latitude,
            .longitude = p->longitude,
            .radius_meters = p->radius_meters
        }
    };
    strlcpy(msg.data.add_location.name, p->name, sizeof(msg.data.add_location.name));
    return (xQueueSend(s_ctx.queue, &msg, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_gps_set_location_name(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const gps_set_name_params_t *p = &params->gps_set_name;
    gps_msg_t msg = {
        .cmd = GPS_CMD_SET_NAME
    };
    strlcpy(msg.data.set_name.name, p->name, sizeof(msg.data.set_name.name));
    return (xQueueSend(s_ctx.queue, &msg, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

/* Wrappers for command router (void* params) */
static esp_err_t gps_start_wrapper(void *context, void *params)
{
    return cmd_gps_start(context, (const command_param_union_t*)params);
}
static esp_err_t gps_stop_wrapper(void *context, void *params)
{
    return cmd_gps_stop(context, (const command_param_union_t*)params);
}
static esp_err_t gps_get_last_fix_wrapper(void *context, void *params)
{
    return cmd_gps_get_last_fix(context, (const command_param_union_t*)params);
}
static esp_err_t gps_add_known_location_wrapper(void *context, void *params)
{
    return cmd_gps_add_known_location(context, (const command_param_union_t*)params);
}
static esp_err_t gps_set_location_name_wrapper(void *context, void *params)
{
    return cmd_gps_set_location_name(context, (const command_param_union_t*)params);
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t gps_service_init(void)
{
    if (s_ctx.queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Create command queue */
    s_ctx.queue = xQueueCreate(10, sizeof(gps_msg_t));
    if (!s_ctx.queue) {
        return ESP_ERR_NO_MEM;
    }

    /* Create GPS driver */
    gps_config_t cfg = {
        .uart_num = GPS_UART_NUM,          /* Adjust to your hardware */
        .tx_pin = GPS_TX_PIN,           /* GPS RX (to ESP TX) – adjust */
        .rx_pin = GPS_RX_PIN,           /* GPS TX (to ESP RX) – adjust */
        .baud_rate = 9600,
        .rx_buffer_size = 1024
    };
    esp_err_t ret = gps_driver_create(&cfg, &s_ctx.driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GPS driver: %d", ret);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ret;
    }

    /* Initialise known location table with default entries */
    memset(s_ctx.known_locations, 0, sizeof(s_ctx.known_locations));
    for (int i = 0; i < DEFAULT_LOCATION_COUNT && i < MAX_KNOWN_LOCATIONS; i++) {
        s_ctx.known_locations[i] = s_default_locations[i];
    }
    s_ctx.known_count = DEFAULT_LOCATION_COUNT;

    /* Create service task */
    BaseType_t ret_task = xTaskCreate(gps_service_task, "gps_svc", GPS_TASK_STACK_SIZE,
                                       NULL, GPS_TASK_PRIORITY, &s_ctx.task);
    if (ret_task != pdPASS) {
        gps_driver_delete(s_ctx.driver);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GPS service initialised");
    return ESP_OK;
}

esp_err_t gps_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(CMD_GPS_START, gps_start_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_GPS_STOP, gps_stop_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_GPS_GET_LAST_FIX, gps_get_last_fix_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_GPS_ADD_KNOWN_LOCATION, gps_add_known_location_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(CMD_GPS_SET_LOCATION_NAME, gps_set_location_name_wrapper, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "GPS command handlers registered");
    return ESP_OK;
}

esp_err_t gps_service_start(void)
{
    /* Start the acquisition (default off) */
    gps_msg_t msg = { .cmd = GPS_CMD_START };
    if (xQueueSend(s_ctx.queue, &msg, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GPS service started");
    return ESP_OK;
}

esp_err_t gps_service_stop(void)
{
    gps_msg_t msg = { .cmd = GPS_CMD_STOP };
    if (xQueueSend(s_ctx.queue, &msg, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GPS service stopped");
    return ESP_OK;
}

bool gps_service_get_last_fix(gps_data_t *data)
{
    if (!data) return false;
    memcpy(data, &s_ctx.last_data, sizeof(gps_data_t));
    return s_ctx.fix_valid;
}