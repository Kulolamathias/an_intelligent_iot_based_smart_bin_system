/**
 * @file components/drivers/sensors/gps_driver/gps_driver.c
 * @brief GPS Driver – implementation for NMEA‑0183 GPS modules.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses UART driver in interrupt mode to receive NMEA sentences.
 * - Parses $GPGGA and $GPRMC sentences (most common for position, time, fix).
 * - Implements checksum validation and basic sentence filtering.
 * - Non‑blocking: `gps_driver_update()` processes any data in the RX buffer.
 * - All parsed data is stored in the handle's internal data structure.
 * =============================================================================
 */

#include "gps_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "GPS_DRV";

/* Internal structure for GPS handle */
struct gps_handle_t {
    uart_port_t uart_num;
    bool started;
    bool initialized;
    gps_data_t data;              /* Latest parsed data */
    char line[128];               /* Buffer for current NMEA sentence */
    uint16_t line_idx;            /* Current index in line buffer */
};

/* Helper: calculate NMEA checksum (XOR of characters between '$' and '*') */
static uint8_t nmea_checksum(const char *sentence)
{
    uint8_t cs = 0;
    /* Skip leading '$' */
    if (*sentence == '$') sentence++;
    while (*sentence && *sentence != '*') {
        cs ^= (uint8_t)*sentence;
        sentence++;
    }
    return cs;
}

/* Helper: parse a comma‑separated field from a sentence */
static char* parse_field(char *sentence, int field_index)
{
    char *ptr = sentence;
    int idx = 0;
    while (*ptr && idx < field_index) {
        if (*ptr == ',') idx++;
        ptr++;
    }
    if (*ptr == '\0') return NULL;
    return ptr;
}

/* Helper: convert string to double with safety */
static double parse_double(const char *str)
{
    if (!str || *str == '\0') return 0.0;
    char *endptr;
    double val = strtod(str, &endptr);
    return (endptr > str) ? val : 0.0;
}

/* Helper: convert string to integer */
static uint32_t parse_uint(const char *str)
{
    if (!str || *str == '\0') return 0;
    char *endptr;
    uint32_t val = strtoul(str, &endptr, 10);
    return (endptr > str) ? val : 0;
}

/* Helper: convert NMEA latitude format (DDMM.MMMMM) to decimal degrees */
static double parse_nmea_latitude(const char *str)
{
    if (!str || *str == '\0') return 0.0;
    /* Format: DDMM.MMMMM (degrees minutes) */
    double lat = parse_double(str);
    int deg = (int)(lat / 100.0);
    double minutes = lat - (deg * 100.0);
    return deg + (minutes / 60.0);
}

/* Helper: parse NMEA longitude (DDDMM.MMMMM) to decimal degrees */
static double parse_nmea_longitude(const char *str)
{
    if (!str || *str == '\0') return 0.0;
    double lon = parse_double(str);
    int deg = (int)(lon / 100.0);
    double minutes = lon - (deg * 100.0);
    return deg + (minutes / 60.0);
}

/* Parse $GPGGA sentence */
static void parse_gga(gps_handle_t handle, char *sentence)
{
    char *field;
    int field_idx = 0;

    /* Fix quality (field 6) */
    field = parse_field(sentence, 6);
    if (field) {
        uint32_t fix_quality = parse_uint(field);
        handle->data.fix_valid = (fix_quality >= 1 && fix_quality <= 5);
    } else {
        handle->data.fix_valid = false;
    }

    /* Latitude (field 2) */
    field = parse_field(sentence, 2);
    if (field && *field) {
        double lat = parse_nmea_latitude(field);
        /* Hemisphere (field 3) */
        field = parse_field(sentence, 3);
        if (field && *field == 'S') lat = -lat;
        handle->data.latitude = lat;
    }

    /* Longitude (field 4) */
    field = parse_field(sentence, 4);
    if (field && *field) {
        double lon = parse_nmea_longitude(field);
        /* Hemisphere (field 5) */
        field = parse_field(sentence, 5);
        if (field && *field == 'W') lon = -lon;
        handle->data.longitude = lon;
    }

    /* Altitude (field 9) */
    field = parse_field(sentence, 9);
    if (field) {
        handle->data.altitude_m = (float)parse_double(field);
    }

    /* Satellites (field 7) */
    field = parse_field(sentence, 7);
    if (field) {
        handle->data.satellites = (uint8_t)parse_uint(field);
    }

    /* HDOP (field 8) */
    field = parse_field(sentence, 8);
    if (field) {
        handle->data.hdop = (float)parse_double(field);
    }

    /* UTC time (field 1) – format HHMMSS.SS */
    field = parse_field(sentence, 1);
    if (field && *field) {
        double t = parse_double(field);
        handle->data.hour   = (uint8_t)(t / 10000);
        handle->data.minute = (uint8_t)((int)t % 10000) / 100;
        handle->data.second = (uint8_t)((int)t % 100);
    }
}

/* Parse $GPRMC sentence */
static void parse_rmc(gps_handle_t handle, char *sentence)
{
    char *field;
    int field_idx = 0;

    /* Fix validity (field 2) – 'A' = active, 'V' = void */
    field = parse_field(sentence, 2);
    if (field) {
        handle->data.fix_valid = (field[0] == 'A');
        handle->data.time_valid = handle->data.fix_valid;
    }

    /* UTC time (field 1) */
    field = parse_field(sentence, 1);
    if (field && *field) {
        double t = parse_double(field);
        handle->data.hour   = (uint8_t)(t / 10000);
        handle->data.minute = (uint8_t)((int)t % 10000) / 100;
        handle->data.second = (uint8_t)((int)t % 100);
    }

    /* Latitude (field 3) */
    field = parse_field(sentence, 3);
    if (field && *field) {
        double lat = parse_nmea_latitude(field);
        /* Hemisphere (field 4) */
        field = parse_field(sentence, 4);
        if (field && *field == 'S') lat = -lat;
        handle->data.latitude = lat;
    }

    /* Longitude (field 5) */
    field = parse_field(sentence, 5);
    if (field && *field) {
        double lon = parse_nmea_longitude(field);
        /* Hemisphere (field 6) */
        field = parse_field(sentence, 6);
        if (field && *field == 'W') lon = -lon;
        handle->data.longitude = lon;
    }

    /* Speed over ground (field 7) – knots, convert to km/h */
    field = parse_field(sentence, 7);
    if (field) {
        double knots = parse_double(field);
        handle->data.speed_kmh = (float)(knots * 1.852);
    }

    /* Course over ground (field 8) */
    field = parse_field(sentence, 8);
    if (field) {
        handle->data.course_deg = (float)parse_double(field);
    }

    /* Date (field 9) – DDMMYY */
    field = parse_field(sentence, 9);
    if (field && *field) {
        uint32_t date = parse_uint(field);
        handle->data.day   = (uint8_t)(date / 10000);
        handle->data.month = (uint8_t)((date % 10000) / 100);
        handle->data.year  = 2000 + (uint8_t)(date % 100);
    }
}

/* Process a complete NMEA sentence */
static void process_sentence(gps_handle_t handle, char *sentence)
{
    /* Validate checksum if present */
    char *star = strchr(sentence, '*');
    if (star) {
        *star = '\0';
        uint8_t rx_cs = (uint8_t)strtoul(star + 1, NULL, 16);
        uint8_t calc_cs = nmea_checksum(sentence);
        if (rx_cs != calc_cs) {
            ESP_LOGD(TAG, "Checksum mismatch: expected %02X, got %02X", calc_cs, rx_cs);
            return;
        }
    }

    /* Check sentence type */
    if (strncmp(sentence, "$GPGGA", 6) == 0) {
        parse_gga(handle, sentence);
    } else if (strncmp(sentence, "$GPRMC", 6) == 0) {
        parse_rmc(handle, sentence);
    }
    /* Ignore other sentences */
}

/* Public API */
esp_err_t gps_driver_create(const gps_config_t *cfg, gps_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    gps_handle_t handle = calloc(1, sizeof(struct gps_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->uart_num = cfg->uart_num;
    handle->started = false;
    handle->initialized = false;
    handle->line_idx = 0;
    memset(handle->line, 0, sizeof(handle->line));
    memset(&handle->data, 0, sizeof(handle->data));

    /* Configure UART */
    uart_config_t uart_cfg = {
        .baud_rate = cfg->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_param_config(handle->uart_num, &uart_cfg);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }

    ret = uart_set_pin(handle->uart_num, cfg->tx_pin, cfg->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }

    ret = uart_driver_install(handle->uart_num, cfg->rx_buffer_size, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }

    handle->initialized = true;
    *out_handle = handle;
    ESP_LOGI(TAG, "GPS driver initialised (UART %d, baud %lu)", handle->uart_num, cfg->baud_rate);
    return ESP_OK;
}

esp_err_t gps_driver_start(gps_handle_t handle)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (handle->started) return ESP_OK;

    /* Flush any stale data */
    uart_flush_input(handle->uart_num);
    handle->started = true;
    ESP_LOGI(TAG, "GPS driver started");
    return ESP_OK;
}

esp_err_t gps_driver_update(gps_handle_t handle)
{
    if (!handle || !handle->initialized || !handle->started) return ESP_ERR_INVALID_STATE;

    int len = uart_read_bytes(handle->uart_num, (uint8_t*)handle->line + handle->line_idx,
                               sizeof(handle->line) - handle->line_idx - 1, 0);
    if (len <= 0) return ESP_ERR_TIMEOUT;

    handle->line_idx += len;
    handle->line[handle->line_idx] = '\0';

    /* Look for complete sentences */
    char *start = handle->line;
    char *end;
    while ((end = strchr(start, '\n')) != NULL) {
        *end = '\0';
        /* Skip carriage return if present */
        char *cr = strchr(start, '\r');
        if (cr) *cr = '\0';
        /* Process sentence if it starts with '$' */
        if (start[0] == '$') {
            process_sentence(handle, start);
        }
        /* Move to next line */
        start = end + 1;
    }
    /* Move remaining partial line to beginning */
    if (start != handle->line) {
        memmove(handle->line, start, handle->line_idx - (start - handle->line));
        handle->line_idx -= (start - handle->line);
    } else {
        handle->line_idx = 0;
    }
    return ESP_OK;
}

esp_err_t gps_driver_get_data(gps_handle_t handle, gps_data_t *data)
{
    if (!handle || !handle->initialized || !data) return ESP_ERR_INVALID_ARG;
    memcpy(data, &handle->data, sizeof(gps_data_t));
    data->timestamp_ms = esp_timer_get_time() / 1000;
    return ESP_OK;
}

esp_err_t gps_driver_stop(gps_handle_t handle)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (!handle->started) return ESP_OK;

    uart_flush_input(handle->uart_num);
    handle->started = false;
    ESP_LOGI(TAG, "GPS driver stopped");
    return ESP_OK;
}

esp_err_t gps_driver_delete(gps_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    if (handle->started) gps_driver_stop(handle);
    uart_driver_delete(handle->uart_num);
    free(handle);
    ESP_LOGI(TAG, "GPS driver deleted");
    return ESP_OK;
}