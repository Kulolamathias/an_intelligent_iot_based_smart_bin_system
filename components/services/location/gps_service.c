/**
 * @file   components/services/location/gps_service.c
 * @brief  Implementation of the GPS Service.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the service defined in gps_service.h. The core logic relies on
 * the deterministic comma‑split NMEA parser originally proven in
 * main/gps_proof.c.
 *
 * =============================================================================
 * @author  Matthithyahu
 * @date    2026/05/11
 */

#include "gps_service.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "gps_driver.h"              /* Phase 1 driver */
#include "service_interfaces.h"     /* service_post_event, service_register_command */
#include "event_types.h"            /* gps_fix_t, system_event_t, etc. */
#include "command_types.h"          /* CMD_GPS_START, CMD_GPS_STOP */

/* -------------------------------------------------------------------------- */
/* Configuration (hard‑coded until Kconfig is introduced)                    */
/* -------------------------------------------------------------------------- */

#define GPS_UART_PORT          UART_NUM_2
#define GPS_TXD_PIN            GPIO_NUM_19
#define GPS_RXD_PIN            GPIO_NUM_18
#define GPS_BAUDRATE           9600
#define GPS_UART_RX_BUF_SIZE   512

/** Polling interval: 200 ms (5 Hz) – matches NMEA output rate of 1 Hz comfortably */
#define GPS_POLL_PERIOD_MS     200

/** Number of consecutive failed reads before posting fix‑lost event */
#define FIX_LOSS_COUNT         50   /* 50 * 200 ms = 10 s */

/* -------------------------------------------------------------------------- */
/* NMEA parser constants (from gps_proof.c)                                  */
/* -------------------------------------------------------------------------- */

#define NMEA_LINE_BUF_SIZE     256
#define UART_READ_CHUNK_SIZE   16
#define UART_READ_TIMEOUT_MS   5
#define MAX_NMEA_FIELDS        20

/* -------------------------------------------------------------------------- */
/* Static context                                                           */
/* -------------------------------------------------------------------------- */

static const char *TAG = "gps_svc";

static gps_driver_config_t s_drv_cfg = {
    .uart_num      = GPS_UART_PORT,
    .tx_pin        = GPS_TXD_PIN,
    .rx_pin        = GPS_RXD_PIN,
    .baud_rate     = GPS_BAUDRATE,
    .rx_buffer_size = GPS_UART_RX_BUF_SIZE,
};

static esp_timer_handle_t s_timer = NULL;      /**< Periodic polling timer */
static SemaphoreHandle_t   s_mutex = NULL;     /**< Protects s_last_fix */

static gps_fix_t s_last_fix = { .valid = false }; /**< Latest fix (mutex) */

static uint32_t s_fix_loss_ctr = 0;            /**< Consecutive no‑fix counts */
static bool     s_fix_lost_event_sent = false; /**< Edge‑trigger for lost event */

/* NMEA line‑buffer (static, no heap) */
static char   s_line_buf[NMEA_LINE_BUF_SIZE];
static size_t s_line_idx = 0;
static bool   s_sentence_started = false;

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                     */
/* -------------------------------------------------------------------------- */

static double raw_to_decimal(double raw, char hemisphere);
static int    split_nmea_fields(char *sentence, char *fields_out[], int max_fields);
static void   parse_nmea_line(char *line);
static void   timer_callback(void *arg);

/* Command handlers */
static esp_err_t cmd_gps_start(void *ctx, void *params);
static esp_err_t cmd_gps_stop(void *ctx, void *params);

/* ====================================================================== */
/* Public API Implementation                                              */
/* ====================================================================== */

esp_err_t gps_service_init(void)
{
    /* Create mutex for last‑fix protection */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Initialise the UART driver */
    esp_err_t ret = gps_driver_init(&s_drv_cfg);
    if (ret != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        ESP_LOGE(TAG, "GPS driver init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create polling timer (initially stopped) */
    const esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .arg      = NULL,
        .name     = "gps_poll"
    };
    ret = esp_timer_create(&timer_args, &s_timer);
    if (ret != ESP_OK) {
        gps_driver_deinit();
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GPS service initialised (UART%u, %u ms poll)",
             (unsigned)GPS_UART_PORT, GPS_POLL_PERIOD_MS);
    return ESP_OK;
}

esp_err_t gps_service_register_handlers(void)
{
    esp_err_t ret;

    ret = service_register_command(CMD_GPS_START, cmd_gps_start, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_GPS_STOP, cmd_gps_stop, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Command handlers registered");
    return ESP_OK;
}

esp_err_t gps_service_start(void)
{
    // /* The timer is started via CMD_GPS_START; nothing to do here */
    // ESP_LOGI(TAG, "GPS service started (waiting for CMD_GPS_START)");
    // return ESP_OK;

    esp_err_t ret = esp_timer_start_periodic(s_timer, GPS_POLL_PERIOD_MS * 1000U);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start GPS polling timer: %s", esp_err_to_name(ret));
        return ret;
    }
    s_fix_loss_ctr = 0;
    s_fix_lost_event_sent = false;
    ESP_LOGI(TAG, "GPS service started (polling %" PRIu32 " ms)", (uint32_t)GPS_POLL_PERIOD_MS);
    return ESP_OK;
}

/* ====================================================================== */
/* Command Handlers                                                       */
/* ====================================================================== */

static esp_err_t cmd_gps_start(void *ctx, void *params)
{
    (void)ctx;
    (void)params;

    /* Start periodic timer */
    esp_err_t ret = esp_timer_start_periodic(s_timer, GPS_POLL_PERIOD_MS * 1000U);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Reset loss counters */
    s_fix_loss_ctr = 0;
    s_fix_lost_event_sent = false;

    ESP_LOGI(TAG, "GPS polling started (periodic %u ms)", GPS_POLL_PERIOD_MS);
    return ESP_OK;
}

static esp_err_t cmd_gps_stop(void *ctx, void *params)
{
    (void)ctx;
    (void)params;

    esp_timer_stop(s_timer);

    /* Mark fix as invalid */
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_last_fix.valid = false;
        xSemaphoreGive(s_mutex);
    }

    s_fix_loss_ctr = 0;
    s_fix_lost_event_sent = false;

    ESP_LOGI(TAG, "GPS polling stopped");
    return ESP_OK;
}

/* ====================================================================== */
/* Timer Callback – polls driver, accumulates NMEA, parses, posts events  */
/* ====================================================================== */

static void timer_callback(void *arg)
{
    (void)arg;

    uint8_t data[UART_READ_CHUNK_SIZE];
    size_t  bytes_read = 0;
    esp_err_t ret = gps_driver_read(data, sizeof(data), &bytes_read,
                                    UART_READ_TIMEOUT_MS);

    if (ret == ESP_OK) {
        for (size_t i = 0; i < bytes_read; i++) {
            char ch = (char)data[i];

            /* Start of sentence */
            if (ch == '$') {
                s_line_idx = 0;
                memset(s_line_buf, 0, sizeof(s_line_buf));
                s_sentence_started = true;
            }

            if (s_sentence_started) {
                if (ch == '\n' || ch == '\r') {
                    if (s_line_idx > 0 && s_line_buf[0] == '$') {
                        s_line_buf[s_line_idx] = '\0';
                        parse_nmea_line(s_line_buf);
                    }
                    /* Reset for next sentence */
                    s_sentence_started = false;
                    s_line_idx = 0;
                    memset(s_line_buf, 0, sizeof(s_line_buf));
                } else if (s_line_idx < (NMEA_LINE_BUF_SIZE - 1)) {
                    s_line_buf[s_line_idx++] = ch;
                } else {
                    /* Buffer overflow – discard line */
                    s_sentence_started = false;
                    s_line_idx = 0;
                    memset(s_line_buf, 0, sizeof(s_line_buf));
                }
            }
        }
    }

    /* --- Process GGA / RMC sentence if a valid one was parsed --------- */
    /* (The parse_nmea_line function handles everything inline) */

    /* --- Handle fix‑loss detection ------------------------------------- */
    if (s_fix_lost_event_sent == false) {
        s_fix_loss_ctr++;
        if (s_fix_loss_ctr >= FIX_LOSS_COUNT) {
            /* Post EVENT_GPS_FIX_LOST */
            system_event_t ev = {
                .id = EVENT_GPS_FIX_LOST,
                .timestamp_us = esp_timer_get_time(),
                .source = 0,
                .data = { { {0} } }
            };
            service_post_event(&ev);
            s_fix_lost_event_sent = true;
            ESP_LOGW(TAG, "GPS fix lost (no valid fix for %u cycles)",
                     (unsigned)s_fix_loss_ctr);
        }
    }
}

/* ====================================================================== */
/* NMEA Parser (extracted and adapted from gps_proof.c)                   */
/* ====================================================================== */

static double raw_to_decimal(double raw, char hemisphere)
{
    if (raw == 0.0) {
        return 0.0;
    }
    int deg = (int)(raw / 100.0);
    double minutes = raw - (deg * 100.0);
    double decimal = deg + (minutes / 60.0);
    if (hemisphere == 'S' || hemisphere == 'W') {
        decimal = -decimal;
    }
    return decimal;
}

static int split_nmea_fields(char *sentence, char *fields_out[], int max_fields)
{
    int count = 0;
    char *start = sentence;
    if (*start == '$') {
        start++;
    }
    fields_out[count++] = start;   /* talker + sentence type */

    char *p = start;
    while (*p && count < max_fields) {
        if (*p == ',') {
            *p = '\0';
            fields_out[count++] = p + 1;
        }
        p++;
    }
    return count;
}

/**
 * @brief Parse a single NMEA sentence (called only for lines starting with '$').
 *
 * If a valid GGA fix is found, updates s_last_fix, posts EVENT_GPS_FIX_UPDATE,
 * and resets fix‑loss counter. If an RMC sentence provides a fix (status='A')
 * but no altitude/satellites, it is accepted; altitude and satellites remain
 * as last known or 0.
 */
static void parse_nmea_line(char *line)
{
    if (line == NULL || line[0] != '$') return;

    char *fields[MAX_NMEA_FIELDS];
    int num_fields = split_nmea_fields(line, fields, MAX_NMEA_FIELDS);
    if (num_fields < 2) return;

    const char *type = fields[0]; /* e.g. "$GPGGA" */
    bool is_gga = (strstr(type, "GGA") != NULL);
    bool is_rmc = (strstr(type, "RMC") != NULL);

    if (!is_gga && !is_rmc) return;

    /* Temporary variables */
    double lat = 0.0, lon = 0.0;
    float  alt = 0.0f, utc_time = 0.0f;
    uint8_t quality = 0, sats = 0;
    bool    fix_obtained = false;

    if (is_gga && num_fields >= 15) {
        char *end;
        float time_f  = strtof(fields[1], &end);
        if (end == fields[1]) time_f = 0.0f;
        float lat_raw = strtof(fields[2], &end);
        if (end == fields[2]) lat_raw = 0.0f;
        char ns = fields[3][0];
        float lon_raw = strtof(fields[4], &end);
        if (end == fields[4]) lon_raw = 0.0f;
        char ew = fields[5][0];
        int qual = (int)strtol(fields[6], &end, 10);
        if (end == fields[6]) qual = 0;
        int sat  = (int)strtol(fields[7], &end, 10);
        if (end == fields[7]) sat = 0;
        float altitude = strtof(fields[9], &end);
        if (end == fields[9]) altitude = 0.0f;

        if (qual > 0) {
            quality = (uint8_t)qual;
            sats    = (uint8_t)sat;
            alt     = altitude;
            utc_time = time_f;
            lat      = raw_to_decimal((double)lat_raw, ns);
            lon      = raw_to_decimal((double)lon_raw, ew);
            fix_obtained = true;
        }
    } else if (is_rmc && num_fields >= 7) {
        char *end;
        float time_f = strtof(fields[1], &end);
        if (end == fields[1]) time_f = 0.0f;
        char status = fields[2][0];
        float lat_raw = strtof(fields[3], &end);
        if (end == fields[3]) lat_raw = 0.0f;
        char ns = fields[4][0];
        float lon_raw = strtof(fields[5], &end);
        if (end == fields[5]) lon_raw = 0.0f;
        char ew = fields[6][0];

        if (status == 'A') {
            lat = raw_to_decimal((double)lat_raw, ns);
            lon = raw_to_decimal((double)lon_raw, ew);
            if (lat != 0.0 && lon != 0.0) {
                utc_time = time_f;
                quality  = 1;   /* assumed fix */
                sats     = 0;
                alt      = 0.0f;
                fix_obtained = true;
            }
        }
    }

    if (fix_obtained) {
        /* Update stored fix under mutex */
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_last_fix.valid       = true;
            s_last_fix.latitude    = lat;
            s_last_fix.longitude   = lon;
            s_last_fix.altitude_m  = alt;
            s_last_fix.satellites  = sats;
            s_last_fix.hdop        = 0.0f;   /* not parsed */
            s_last_fix.speed_kmh   = 0.0f;   /* not parsed */
            s_last_fix.timestamp_ms = esp_timer_get_time() / 1000ULL;
            xSemaphoreGive(s_mutex);
        }

        /* Post EVENT_GPS_FIX_UPDATE */
        system_event_t ev = {
            .id = EVENT_GPS_FIX_UPDATE,
            .timestamp_us = esp_timer_get_time(),
            .source = 0,
            .data = { { {0} } }
        };
        /* Copy into event payload (POD, no pointers) */
        ev.data.gps_fix.valid      = true;
        ev.data.gps_fix.latitude   = lat;
        ev.data.gps_fix.longitude  = lon;
        ev.data.gps_fix.altitude_m = alt;
        ev.data.gps_fix.satellites = sats;
        ev.data.gps_fix.hdop       = 0.0f;
        ev.data.gps_fix.speed_kmh  = 0.0f;
        ev.data.gps_fix.timestamp_ms = s_last_fix.timestamp_ms;
        service_post_event(&ev);

        /* Reset loss counter */
        s_fix_loss_ctr = 0;
        s_fix_lost_event_sent = false;

        ESP_LOGD(TAG, "Fix posted: lat=%.6f lon=%.6f alt=%.1f sats=%u",
                 lat, lon, (double)alt, sats);

        /* ------------------------------------------------------------------
         * PLACE_NAME_FEATURE: Insert coordinate‑to‑name lookup here.
         * ------------------------------------------------------------------ */
    }
}