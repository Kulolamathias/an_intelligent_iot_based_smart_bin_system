/**
 * @file gps_service.c
 * @brief Implementation of the GPS service.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - The service runs a single FreeRTOS task that processes commands and
 *   reads NMEA lines when started.
 * - Parsing is done manually (no sscanf) for efficiency and determinism.
 * - Checksums are validated.
 * - Fix is considered lost if no update within GPS_FIX_TIMEOUT_MS.
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-03-01
 * @author System Architecture Team
 * =============================================================================
 */

#include "gps_service.h"
#include "gps_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include <string.h>
#include <stdlib.h>  // for strtol, strtof (no malloc)

static const char *TAG = "GPS_SVC";

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
#define GPS_SVC_QUEUE_SIZE      5
#define GPS_SVC_TASK_STACK_SIZE 4096
#define GPS_SVC_TASK_PRIORITY   5
#define GPS_LINE_BUFFER_SIZE    128
#define GPS_FIX_TIMEOUT_MS      5000   /* 5 seconds without update = lost */

/* UART configuration (hardcoded for now; could be made configurable) */
#define GPS_UART_NUM     UART_NUM_2
#define GPS_UART_BAUD     9600
#define GPS_UART_TX_PIN   17     /* not used */
#define GPS_UART_RX_PIN   16     /* example */

/* -------------------------------------------------------------------------
 * Internal event types (for service's own queue)
 * ------------------------------------------------------------------------- */
typedef enum {
    INT_EVT_CMD_START,
    INT_EVT_CMD_STOP,
    INT_EVT_CMD_GET_LAST_FIX,
    INT_EVT_LINE_READY,      /* not used directly; we'll read lines in task loop */
} internal_event_id_t;

typedef struct {
    internal_event_id_t id;
    /* no payload for these commands */
} internal_event_t;

/* -------------------------------------------------------------------------
 * Service context (static)
 * ------------------------------------------------------------------------- */
typedef struct {
    TaskHandle_t task;                 /**< Service task handle */
    QueueHandle_t queue;                /**< Internal command queue */
    gps_driver_handle_t driver;         /**< Handle to GPS driver */
    bool started;                        /**< true if reading is active */
    gps_fix_t current_fix;               /**< Last valid fix */
    uint64_t last_update_ms;              /**< System time of last valid fix (ms) */
    bool fix_valid;                       /**< true if fix is currently valid */
} gps_service_ctx_t;

static gps_service_ctx_t s_ctx = {0};

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
static void gps_service_task(void *pvParameters);
static void process_command(const internal_event_t *ev);
static void parse_nmea_line(const char *line, size_t len);
static bool validate_checksum(const char *line);
static double parse_degrees(const char *str, char dir);
static uint64_t get_time_ms(void);

/* -------------------------------------------------------------------------
 * Command handlers (registered with command router)
 * ------------------------------------------------------------------------- */
static esp_err_t handle_gps_start(void *context, void *params)
{
    (void)context;
    (void)params;
    internal_event_t ev = { .id = INT_EVT_CMD_START };
    if (xQueueSend(s_ctx.queue, &ev, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t handle_gps_stop(void *context, void *params)
{
    (void)context;
    (void)params;
    internal_event_t ev = { .id = INT_EVT_CMD_STOP };
    if (xQueueSend(s_ctx.queue, &ev, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t handle_gps_get_last_fix(void *context, void *params)
{
    (void)context;
    (void)params;
    internal_event_t ev = { .id = INT_EVT_CMD_GET_LAST_FIX };
    if (xQueueSend(s_ctx.queue, &ev, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*============================================================================
 * Public API (service manager lifecycle)
 *============================================================================*/

esp_err_t gps_service_init(void)
{
    if (s_ctx.queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Create internal command queue */
    s_ctx.queue = xQueueCreate(GPS_SVC_QUEUE_SIZE, sizeof(internal_event_t));
    if (!s_ctx.queue) {
        return ESP_ERR_NO_MEM;
    }

    /* Initialize GPS driver */
    esp_err_t ret = gps_driver_create(GPS_UART_NUM, GPS_UART_BAUD,
                                      GPS_UART_TX_PIN, GPS_UART_RX_PIN,
                                      &s_ctx.driver);
    if (ret != ESP_OK) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ret;
    }

    /* Start driver (UART reception) */
    ret = gps_driver_start(s_ctx.driver);
    if (ret != ESP_OK) {
        gps_driver_delete(s_ctx.driver);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ret;
    }

    /* Create service task */
    BaseType_t ret_task = xTaskCreate(gps_service_task, "gps_svc",
                                      GPS_SVC_TASK_STACK_SIZE, NULL,
                                      GPS_SVC_TASK_PRIORITY, &s_ctx.task);
    if (ret_task != pdPASS) {
        gps_driver_stop(s_ctx.driver);
        gps_driver_delete(s_ctx.driver);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ctx.started = false;
    s_ctx.fix_valid = false;
    memset(&s_ctx.current_fix, 0, sizeof(gps_fix_t));

    ESP_LOGI(TAG, "GPS service initialised");
    return ESP_OK;
}

esp_err_t gps_service_register_handlers(void)
{
    esp_err_t ret;

    ret = service_register_command(CMD_GPS_START, handle_gps_start, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_GPS_STOP, handle_gps_stop, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_GPS_GET_LAST_FIX, handle_gps_get_last_fix, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "GPS command handlers registered");
    return ESP_OK;
}

esp_err_t gps_service_start(void)
{
    ESP_LOGI(TAG, "GPS service started");
    return ESP_OK;
}

/*============================================================================
 * Service task
 *============================================================================*/
static void gps_service_task(void *pvParameters)
{
    (void)pvParameters;
    internal_event_t ev;
    char line[GPS_LINE_BUFFER_SIZE];
    int line_len;

    while (1) {
        /* Check for commands with a short timeout (50 ms) */
        if (xQueueReceive(s_ctx.queue, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            process_command(&ev);
        }

        /* If started, attempt to read a line from the driver (non-blocking) */
        if (s_ctx.started) {
            /* Use a short timeout to avoid blocking */
            line_len = gps_driver_read_line(s_ctx.driver, line, sizeof(line), 0);
            if (line_len > 0) {
                ESP_LOGI(TAG, "Raw NMEA: %s", line);
                parse_nmea_line(line, line_len);
            }
        }

        /* Check for fix timeout */
        uint64_t now = get_time_ms();
        if (s_ctx.fix_valid && (now - s_ctx.last_update_ms > GPS_FIX_TIMEOUT_MS)) {
            s_ctx.fix_valid = false;
            ESP_LOGI(TAG, "GPS fix lost");
            system_event_t ev = {
                .id = EVENT_GPS_FIX_LOST,
                .data = { { {0} } }   // zero payload /**<TODO: to be reviewed (this line) */
            };
            service_post_event(&ev);
        }
    }
}

static void process_command(const internal_event_t *ev)
{
    switch (ev->id) {
        case INT_EVT_CMD_START:
            if (!s_ctx.started) {
                s_ctx.started = true;
                ESP_LOGI(TAG, "GPS reading started");
            }
            break;

        case INT_EVT_CMD_STOP:
            if (s_ctx.started) {
                s_ctx.started = false;
                ESP_LOGI(TAG, "GPS reading stopped");
            }
            break;

        case INT_EVT_CMD_GET_LAST_FIX:
            if (s_ctx.fix_valid) {
                system_event_t sys_ev = {
                    .id = EVENT_GPS_FIX_UPDATED,
                    .data = { .gps_fix = s_ctx.current_fix }
                };
                service_post_event(&sys_ev);
            }
            break;

        default:
            break;
    }
}

/*============================================================================
 * NMEA Parsing Helpers
 *============================================================================*/

static bool validate_checksum(const char *line)
{
    /* Format: $...*hh */
    const char *star = strchr(line, '*');
    if (!star) return false;
    if (star - line < 3) return false;  /* too short */

    uint8_t calc = 0;
    for (const char *p = line + 1; p < star; p++) {
        calc ^= *p;
    }
    uint8_t msg = (uint8_t)strtol(star + 1, NULL, 16);
    return calc == msg;
}

static double parse_degrees(const char *str, char dir)
{
    /* Format: ddmm.mmmm or dddmm.mmmm */
    char *end;
    double val = strtod(str, &end);
    int deg = (int)(val / 100);
    double min = val - deg * 100;
    double dec = deg + min / 60.0;
    if (dir == 'S' || dir == 'W') {
        dec = -dec;
    }
    return dec;
}

static void parse_gga(const char *line)
{
    /* $GPGGA,time,lat,N,lon,E,fix,sat,hdop,alt,M,sep,M,... */
    char buf[128];
    strlcpy(buf, line, sizeof(buf));
    char *token = buf;
    int field = 0;
    char *lat_str = NULL, *lat_dir = NULL, *lon_str = NULL, *lon_dir = NULL;
    char *fix_str = NULL, *sat_str = NULL, *hdop_str = NULL, *alt_str = NULL;

    while ((token = strtok(token, ",")) != NULL) {
        switch (field) {
            case 2: lat_str = token; break;
            case 3: lat_dir = token; break;
            case 4: lon_str = token; break;
            case 5: lon_dir = token; break;
            case 6: fix_str = token; break;
            case 7: sat_str = token; break;
            case 8: hdop_str = token; break;
            case 9: alt_str = token; break;
            default: break;
        }
        field++;
        token = NULL;
    }

    if (field < 15) return;  /* incomplete */

    int fix = atoi(fix_str);
    if (fix == 0) {
        s_ctx.fix_valid = false;  /* no fix */
        return;
    }

    if (lat_str && lat_dir && lon_str && lon_dir) {
        double lat = parse_degrees(lat_str, lat_dir[0]);
        double lon = parse_degrees(lon_str, lon_dir[0]);
        s_ctx.current_fix.latitude = lat;
        s_ctx.current_fix.longitude = lon;
    }
    if (sat_str) {
        s_ctx.current_fix.satellites = atoi(sat_str);
    }
    if (hdop_str) {
        s_ctx.current_fix.hdop = strtof(hdop_str, NULL);
    }
    if (alt_str) {
        s_ctx.current_fix.altitude_m = strtof(alt_str, NULL);
    }
    s_ctx.current_fix.valid = true;
    s_ctx.fix_valid = true;
    s_ctx.last_update_ms = get_time_ms();

    ESP_LOGI(TAG, "GGA parsed: lat=%.6f, lon=%.6f, alt=%.1f", s_ctx.current_fix.latitude, s_ctx.current_fix.longitude, s_ctx.current_fix.altitude_m);
}

static void parse_rmc(const char *line)
{
    /* $GPRMC,time,status,lat,N,lon,E,speed,course,date,... */
    char buf[128];
    strlcpy(buf, line, sizeof(buf));
    char *token = buf;
    int field = 0;
    char *status = NULL;
    char *lat_str = NULL, *lat_dir = NULL, *lon_str = NULL, *lon_dir = NULL;
    char *speed_str = NULL;

    while ((token = strtok(token, ",")) != NULL) {
        switch (field) {
            case 2: status = token; break;
            case 3: lat_str = token; break;
            case 4: lat_dir = token; break;
            case 5: lon_str = token; break;
            case 6: lon_dir = token; break;
            case 7: speed_str = token; break;
            default: break;
        }
        field++;
        token = NULL;
    }

    if (field < 12) return;

    if (status && status[0] != 'A') {
        s_ctx.fix_valid = false;  /* not active */
        return;
    }

    if (lat_str && lat_dir && lon_str && lon_dir) {
        double lat = parse_degrees(lat_str, lat_dir[0]);
        double lon = parse_degrees(lon_str, lon_dir[0]);
        s_ctx.current_fix.latitude = lat;
        s_ctx.current_fix.longitude = lon;
    }
    if (speed_str) {
        float speed_knots = strtof(speed_str, NULL);
        s_ctx.current_fix.speed_kmh = speed_knots * 1.852f;
    }
    s_ctx.current_fix.valid = true;
    s_ctx.fix_valid = true;
    s_ctx.last_update_ms = get_time_ms();
}

static void parse_nmea_line(const char *line, size_t len)
{
    (void)len;
    if (line[0] != '$') return;
    if (!validate_checksum(line)) return;

    if (strncmp(line, "$GPGGA", 6) == 0) {
        parse_gga(line);
        /* Emit updated fix event */
        system_event_t ev = {
            .id = EVENT_GPS_FIX_UPDATED,
            .data = { .gps_fix = s_ctx.current_fix }
        };
        service_post_event(&ev);
    } else if (strncmp(line, "$GPRMC", 6) == 0) {
        parse_rmc(line);
        /* Also emit, but RMC may come less frequently; we can emit on GGA only */
    }
}

/*============================================================================
 * Time helper (milliseconds since boot)
 *============================================================================*/
static uint64_t get_time_ms(void)
{
    return (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}