/**
 * @file gps_driver.c
 * @brief Implementation of GPS UART driver.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses ESP‑IDF UART driver with static instance pool.
 * - read_line() implements a simple state machine to accumulate bytes until '\n'.
 * - No dynamic memory allocation; all buffers are static per instance.
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-03-01
 * @author System Architecture Team
 * =============================================================================
 */

#include "gps_driver.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gps_driver";

#define GPS_DRIVER_MAX_LINE_LEN 128   /* Maximum NMEA sentence length */

/* Driver instance context (static pool) */
typedef struct {
    bool in_use;                        /**< true if this slot is allocated */
    int uart_num;                        /**< UART port number */
    bool started;                        /**< true if UART is started */
    char line_buf[GPS_DRIVER_MAX_LINE_LEN]; /**< Buffer for accumulating line */
    size_t line_len;                      /**< Current length in line_buf */
} gps_driver_ctx_t;

static gps_driver_ctx_t s_instances[GPS_DRIVER_MAX_INSTANCES];

/* Helper: find free instance slot */
static gps_driver_handle_t find_free_slot(void)
{
    for (int i = 0; i < GPS_DRIVER_MAX_INSTANCES; i++) {
        if (!s_instances[i].in_use) {
            return i;
        }
    }
    return GPS_DRIVER_HANDLE_INVALID;
}

/* Helper: validate handle and return context */
static gps_driver_ctx_t *get_ctx(gps_driver_handle_t handle)
{
    if (handle < 0 || handle >= GPS_DRIVER_MAX_INSTANCES) {
        return NULL;
    }
    if (!s_instances[handle].in_use) {
        return NULL;
    }
    return &s_instances[handle];
}

/*============================================================================
 * Public API
 *============================================================================*/

esp_err_t gps_driver_create(int uart_num, int baud_rate, int tx_pin, int rx_pin,
                            gps_driver_handle_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    gps_driver_handle_t h = find_free_slot();
    if (h == GPS_DRIVER_HANDLE_INVALID) {
        return ESP_ERR_NO_MEM;
    }

    gps_driver_ctx_t *ctx = &s_instances[h];
    memset(ctx, 0, sizeof(gps_driver_ctx_t));

    /* Configure UART parameters */
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_param_config(uart_num, &uart_config);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Set pins */
    ret = uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Install driver with ring buffer (RX only) */
    ret = uart_driver_install(uart_num, 256, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    ctx->in_use = true;
    ctx->uart_num = uart_num;
    ctx->started = false;   /* not started yet */
    *handle = h;

    ESP_LOGD(TAG, "GPS driver created on UART %d", uart_num);
    return ESP_OK;
}

esp_err_t gps_driver_start(gps_driver_handle_t handle)
{
    gps_driver_ctx_t *ctx = get_ctx(handle);
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    /* UART is already installed; just mark started */
    ctx->started = true;
    return ESP_OK;
}

esp_err_t gps_driver_stop(gps_driver_handle_t handle)
{
    gps_driver_ctx_t *ctx = get_ctx(handle);
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    ctx->started = false;
    return ESP_OK;
}

int gps_driver_read_line(gps_driver_handle_t handle,
                         char *buffer, size_t max_len,
                         uint32_t timeout_ms)
{
    gps_driver_ctx_t *ctx = get_ctx(handle);
    if (!ctx || !buffer || max_len == 0) {
        return -2;  /* error */
    }

    if (!ctx->started) {
        return -2;  /* not started */
    }

    uint32_t elapsed = 0;
    const uint32_t tick_ms = 10;  /* check every 10 ms */
    int bytes_read;

    while (elapsed < timeout_ms) {
        /* Read one byte at a time (simple, but fine for low baud) */
        uint8_t byte;
        bytes_read = uart_read_bytes(ctx->uart_num, &byte, 1, tick_ms / portTICK_PERIOD_MS);
        if (bytes_read > 0) {
            if (byte == '\n') {
                /* End of line: null-terminate and return */
                if (ctx->line_len < max_len) {
                    memcpy(buffer, ctx->line_buf, ctx->line_len);
                    buffer[ctx->line_len] = '\0';
                    int ret_len = ctx->line_len;
                    ctx->line_len = 0;  /* reset for next line */
                    return ret_len;
                } else {
                    /* Buffer too small: discard line and return error */
                    ctx->line_len = 0;
                    return -2;
                }
            } else if (byte != '\r') {  /* ignore carriage return */
                if (ctx->line_len < GPS_DRIVER_MAX_LINE_LEN - 1) {
                    ctx->line_buf[ctx->line_len++] = (char)byte;
                } else {
                    /* line too long: discard and reset */
                    ctx->line_len = 0;
                }
            }
        }
        elapsed += tick_ms;
    }

    /* Timeout occurred */
    return -1;
}

esp_err_t gps_driver_delete(gps_driver_handle_t handle)
{
    gps_driver_ctx_t *ctx = get_ctx(handle);
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    uart_driver_delete(ctx->uart_num);
    memset(ctx, 0, sizeof(gps_driver_ctx_t));
    ESP_LOGD(TAG, "GPS driver deleted on UART %d", ctx->uart_num);
    return ESP_OK;
}