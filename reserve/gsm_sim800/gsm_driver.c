/**
 * @file components/drivers/comms/gsm_sim800/gsm_driver.c
 * @brief GSM SIM800 Driver – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses UART driver in interrupt mode.
 * - Command sending flushes the UART buffer before transmission.
 * - Response collection uses a static buffer per handle (mutex‑protected).
 * - SMS sending follows the standard AT+CMGS procedure.
 *
 * @version 1.0.0
 * @author System Architecture Team
 * @date 2026
 */

#include "gsm_driver.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "GSM_DRV";

/* Pseudo‑handle structure (opaque to caller) */
struct gsm_driver_handle_t {
    uart_port_t uart_num;
    uint32_t cmd_timeout_ms;
    uint8_t retry_count;
    bool initialized;
    SemaphoreHandle_t mutex;
    char *line_buffer;
    size_t line_buffer_size;
};

/* Forward declarations */
static esp_err_t gsm_uart_flush_input(uart_port_t uart_num);
static esp_err_t wait_for_response(gsm_handle_t handle, const char *expected, char *response,
                                   size_t resp_size, uint32_t timeout_ms);
static esp_err_t send_at_core(gsm_handle_t handle, const char *cmd, const char *expected,
                              char *response, size_t resp_size, uint32_t timeout_ms);

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static esp_err_t gsm_uart_flush_input(uart_port_t uart_num)
{
    uint8_t dummy[64];
    int len;
    do {
        len = uart_read_bytes(uart_num, dummy, sizeof(dummy), pdMS_TO_TICKS(10));
    } while (len > 0);
    return ESP_OK;
}

static esp_err_t wait_for_response(gsm_handle_t handle, const char *expected, char *response,
                                   size_t resp_size, uint32_t timeout_ms)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (!expected) return ESP_ERR_INVALID_ARG;

    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    char *buf = handle->line_buffer;
    size_t buf_len = handle->line_buffer_size;
    int pos = 0;

    memset(buf, 0, buf_len);

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start) < timeout_ms) {
        int len = uart_read_bytes(handle->uart_num, (uint8_t *)buf + pos,
                                  buf_len - pos - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            pos += len;
            buf[pos] = '\0';

            if (strstr(buf, expected) != NULL) {
                if (response && resp_size > 0) {
                    strlcpy(response, buf, resp_size);
                }
                return ESP_OK;
            }
            if (strstr(buf, "ERROR") != NULL) {
                if (response && resp_size > 0) strlcpy(response, buf, resp_size);
                return ESP_FAIL;  // module returned explicit ERROR
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (response && resp_size > 0) strlcpy(response, buf, resp_size);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t send_at_core(gsm_handle_t handle, const char *cmd, const char *expected,
                              char *response, size_t resp_size, uint32_t timeout_ms)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (!cmd) return ESP_ERR_INVALID_ARG;

    /* Flush UART before sending */
    gsm_uart_flush_input(handle->uart_num);

    /* Send command with \r */
    char cmd_with_r[128];
    snprintf(cmd_with_r, sizeof(cmd_with_r), "%s\r", cmd);
    int len = strlen(cmd_with_r);
    int written = uart_write_bytes(handle->uart_num, cmd_with_r, len);
    if (written != len) return ESP_FAIL;

    if (expected) {
        return wait_for_response(handle, expected, response, resp_size, timeout_ms);
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

esp_err_t gsm_driver_create(const gsm_driver_config_t *config, gsm_handle_t *out_handle)
{
    if (!config || !out_handle) return ESP_ERR_INVALID_ARG;

    gsm_handle_t handle = calloc(1, sizeof(struct gsm_driver_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->uart_num = config->uart_num;
    handle->cmd_timeout_ms = config->cmd_timeout_ms > 0 ? config->cmd_timeout_ms : 30000;
    handle->retry_count = config->retry_count > 0 ? config->retry_count : 2;
    handle->line_buffer_size = 512;
    handle->line_buffer = malloc(handle->line_buffer_size);
    if (!handle->line_buffer) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }
    handle->mutex = xSemaphoreCreateMutex();
    if (!handle->mutex) {
        free(handle->line_buffer);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    /* UART configuration */
    uart_config_t uart_cfg = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_param_config(handle->uart_num, &uart_cfg);
    if (ret != ESP_OK) goto fail;

    ret = uart_set_pin(handle->uart_num, config->tx_pin, config->rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) goto fail;

    ret = uart_driver_install(handle->uart_num, config->rx_buffer_size,
                              config->rx_buffer_size, 0, NULL, 0);
    if (ret != ESP_OK) goto fail;

    handle->initialized = true;
    ESP_LOGI(TAG, "UART initialised (port %d, baud %lu)", handle->uart_num, config->baud_rate);

    /* Test AT communication with retries */
    bool at_ok = false;
    for (int i = 0; i < handle->retry_count; i++) {
        ret = send_at_core(handle, "AT", "OK", NULL, 0, handle->cmd_timeout_ms);
        if (ret == ESP_OK) {
            at_ok = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!at_ok) {
        ESP_LOGE(TAG, "Module not responding to AT");
        ret = ESP_FAIL;
        goto fail;
    }

    /* Basic configuration: echo off, text mode, new SMS indication */
    send_at_core(handle, "ATE0", "OK", NULL, 0, 5000);
    send_at_core(handle, "AT+CMGF=1", "OK", NULL, 0, 5000);
    send_at_core(handle, "AT+CNMI=2,1,0,0,0", "OK", NULL, 0, 5000);

    ESP_LOGI(TAG, "GSM driver ready");
    *out_handle = handle;
    return ESP_OK;

fail:
    if (handle->mutex) vSemaphoreDelete(handle->mutex);
    if (handle->line_buffer) free(handle->line_buffer);
    free(handle);
    return ret;
}

esp_err_t gsm_driver_send_at(gsm_handle_t handle,
                             const char *cmd,
                             const char *expected_response,
                             char *response,
                             size_t resp_size,
                             uint32_t timeout_ms)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (!cmd) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = send_at_core(handle, cmd, expected_response,
                                 response, resp_size,
                                 timeout_ms ? timeout_ms : handle->cmd_timeout_ms);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t gsm_driver_read_line(gsm_handle_t handle,
                               char *line,
                               size_t line_size,
                               uint32_t timeout_ms)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (!line || line_size == 0) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Read until newline */
    size_t idx = 0;
    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    esp_err_t ret = ESP_ERR_TIMEOUT;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start) < timeout_ms) {
        uint8_t ch;
        int len = uart_read_bytes(handle->uart_num, &ch, 1, pdMS_TO_TICKS(100));
        if (len == 1) {
            if (ch == '\n') {
                line[idx] = '\0';
                ret = ESP_OK;
                ESP_LOGI(TAG, "RAW LINE: %s", line);
                break;
            } else if (ch != '\r' && idx < line_size - 1) {
                line[idx++] = (char)ch;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t gsm_driver_send_sms(gsm_handle_t handle,
                              const char *phone_number,
                              const char *message)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (!phone_number || !message) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(handle->cmd_timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Set text mode again to be safe */
    send_at_core(handle, "AT+CMGF=1", "OK", NULL, 0, 5000);

    /* Send AT+CMGS command */
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", phone_number);
    esp_err_t ret = send_at_core(handle, cmd, ">", NULL, 0, 10000);
    if (ret != ESP_OK) {
        xSemaphoreGive(handle->mutex);
        return ret;
    }

    /* Send message text followed by Ctrl+Z (0x1A) */
    char full_msg[200];
    snprintf(full_msg, sizeof(full_msg), "%s\x1A", message);
    int len = strlen(full_msg);
    int written = uart_write_bytes(handle->uart_num, full_msg, len);
    if (written != len) {
        xSemaphoreGive(handle->mutex);
        return ESP_FAIL;
    }

    /* Wait for +CMGS: or OK */
    ret = wait_for_response(handle, "+CMGS:", NULL, 0, 30000);
    if (ret != ESP_OK) {
        ret = wait_for_response(handle, "OK", NULL, 0, 10000);
    }

    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t gsm_driver_delete(gsm_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle->initialized) {
        uart_driver_delete(handle->uart_num);
    }
    if (handle->mutex) vSemaphoreDelete(handle->mutex);
    if (handle->line_buffer) free(handle->line_buffer);
    free(handle);
    ESP_LOGI(TAG, "GSM driver deleted");
    return ESP_OK;
}