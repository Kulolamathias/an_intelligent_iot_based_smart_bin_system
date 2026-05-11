/**
 * @file   components/drivers/sensors/gps/gps_driver.c
 * @brief  Implementation of the GPS UART driver.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the hardware abstraction declared in gps_driver.h. All UART
 * operations are performed through ESP‑IDF v5.4.2 non‑deprecated APIs.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 *  - UART is opened with 8N1, 9600 baud, no flow control, source clock APB
 *  - RX ring buffer size is fixed at 512 bytes (proven sufficient)
 *  - TX is not used (GPS module only transmits)
 *  - The driver maintains a minimal static context (single instance)
 *
 * @author  Matthithyahu
 * @date    2026/05/11
 */

#include "gps_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* -------------------------------------------------------------------------- */
/* Internal constants                                                        */
/* -------------------------------------------------------------------------- */

/** UART data configuration (8N1) */
#define UART_DATA_BITS UART_DATA_8_BITS
#define UART_PARITY    UART_PARITY_DISABLE
#define UART_STOP_BITS UART_STOP_BITS_1
#define UART_FLOW_CTRL UART_HW_FLOWCTRL_DISABLE
#define UART_SOURCE_CLK UART_SCLK_APB

/** Default RX buffer size (hardware ring buffer) */
#define GPS_UART_RX_BUF_SIZE 512

/** Tag for ESP_LOG */
static const char *TAG = "gps_drv";

/* -------------------------------------------------------------------------- */
/* Static driver state                                                       */
/* -------------------------------------------------------------------------- */

static bool s_initialised = false;      /**< true after successful init */
static uart_port_t s_uart_port;         /**< UART port used */

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

esp_err_t gps_driver_init(const gps_driver_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Already initialised → deinit first for clean restart */
    if (s_initialised) {
        gps_driver_deinit();
    }

    /* --- UART configuration structure --- */
    const uart_config_t uart_cfg = {
        .baud_rate  = cfg->baud_rate,
        .data_bits  = UART_DATA_BITS,
        .parity     = UART_PARITY,
        .stop_bits  = UART_STOP_BITS,
        .flow_ctrl  = UART_FLOW_CTRL,
        .source_clk = UART_SOURCE_CLK,
    };

    /* Install UART driver (no TX buffer, no event queue) */
    esp_err_t ret = uart_driver_install(cfg->uart_num,
                                        cfg->rx_buffer_size,
                                        0,        /* TX buffer size (unused) */
                                        0,        /* no event queue */
                                        NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Apply UART parameters */
    ret = uart_param_config(cfg->uart_num, &uart_cfg);
    if (ret != ESP_OK) {
        uart_driver_delete(cfg->uart_num);
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Assign GPIO pins */
    ret = uart_set_pin(cfg->uart_num,
                       cfg->tx_pin,
                       cfg->rx_pin,
                       UART_PIN_NO_CHANGE,  /* no RTS */
                       UART_PIN_NO_CHANGE); /* no CTS */
    if (ret != ESP_OK) {
        uart_driver_delete(cfg->uart_num);
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_uart_port   = cfg->uart_num;
    s_initialised = true;

    ESP_LOGI(TAG, "GPS driver initialised (UART%u, TX:%d, RX:%d, baud:%d)",
             (unsigned)cfg->uart_num, (int)cfg->tx_pin, (int)cfg->rx_pin,
             cfg->baud_rate);
    return ESP_OK;
}

esp_err_t gps_driver_read(uint8_t *buf, size_t max_len, size_t *out_len,
                          uint32_t timeout_ms)
{
    if (buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Convert milliseconds to FreeRTOS ticks (non‑blocking if 0) */
    TickType_t ticks = (timeout_ms > 0) ? pdMS_TO_TICKS(timeout_ms) : 0;

    int bytes_read = uart_read_bytes(s_uart_port, buf, max_len, ticks);
    if (bytes_read < 0) {
        *out_len = 0;
        ESP_LOGE(TAG, "uart_read_bytes error");
        return ESP_FAIL;
    }

    *out_len = (size_t)bytes_read;

    if (bytes_read == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t gps_driver_deinit(void)
{
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = uart_driver_delete(s_uart_port);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "uart_driver_delete returned %s", esp_err_to_name(ret));
        /* Continue as the HW may be in a recoverable state */
    }

    s_initialised = false;
    ESP_LOGI(TAG, "GPS driver deinitialised");
    return ESP_OK;
}