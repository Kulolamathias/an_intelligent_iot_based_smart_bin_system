/**
 * @file gsm_sim800_driver.c
 * @brief SIM800 low-level driver implementation.
 *
 * =============================================================================
 * This file contains only hardware abstraction: UART, AT command exchange,
 * and minimal parsing of SMS responses. No authentication, no queuing.
 * =============================================================================
 */

#include "gsm_sim800_driver.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <ctype.h>

#define RESPONSE_BUFFER_SIZE 4096

static const char *TAG = "GSM_DRV";

/* Default configuration */
static const gsm_driver_config_t DEFAULT_CONFIG = {
    .uart_port = UART_NUM_1,
    .tx_pin = GPIO_NUM_17,
    .rx_pin = GPIO_NUM_16,
    .baud_rate = 9600,
    .buf_size = 2048,
    .cmd_timeout_ms = 30000,
    .retry_count = 3,
};

/* Driver context */
typedef struct {
    gsm_driver_config_t config;
    SemaphoreHandle_t mutex;
    bool initialized;
    bool module_ready;
    char response_buf[RESPONSE_BUFFER_SIZE];   // large enough for AT+CMGL responses
} gsm_driver_ctx_t;

static gsm_driver_ctx_t s_ctx = {0};

/* ============================================================
 * Internal helpers
 * ============================================================ */

static esp_err_t clear_uart_buffer(void)
{
    uint8_t tmp[128];
    int len;
    do {
        len = uart_read_bytes(s_ctx.config.uart_port, tmp, sizeof(tmp), pdMS_TO_TICKS(20));
    } while (len > 0);
    return ESP_OK;
}

static esp_err_t wait_for_response(const char *expected, uint32_t timeout_ms,
                                   char *out_buf, size_t out_size)
{
    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    size_t pos = 0;
    if (out_buf && out_size > 0) out_buf[0] = '\0';

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start) < timeout_ms) {
        int len = uart_read_bytes(s_ctx.config.uart_port,
                                  (uint8_t*)s_ctx.response_buf + pos,
                                  sizeof(s_ctx.response_buf) - pos - 1,
                                  pdMS_TO_TICKS(50));
        if (len > 0) {
            pos += len;
            s_ctx.response_buf[pos] = '\0';
            // Check for expected
            if (strstr(s_ctx.response_buf, expected) != NULL) {
                if (out_buf && out_size > 0) {
                    strncpy(out_buf, s_ctx.response_buf, out_size - 1);
                    out_buf[out_size - 1] = '\0';
                }
                return ESP_OK;
            }
            // Check for ERROR
            if (strstr(s_ctx.response_buf, "ERROR") != NULL) {
                ESP_LOGE(TAG, "Module returned ERROR");
                return ESP_FAIL;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "Timeout waiting for '%s'", expected);
    return ESP_ERR_TIMEOUT;
}

// esp_err_t gsm_driver_send_at(const char *cmd, const char *expected,
//                              uint32_t timeout_ms, char *response, size_t resp_size)
// {
//     if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
//     if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
//         return ESP_ERR_TIMEOUT;
//     }

//     char full_cmd[64];
//     snprintf(full_cmd, sizeof(full_cmd), "%s\r", cmd);
//     ESP_LOGD(TAG, "TX: %s", cmd);

//     clear_uart_buffer();
//     uart_write_bytes(s_ctx.config.uart_port, full_cmd, strlen(full_cmd));

//     esp_err_t ret = wait_for_response(expected, timeout_ms, response, resp_size);
//     xSemaphoreGive(s_ctx.mutex);
//     return ret;
// }

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t gsm_driver_init(const gsm_driver_config_t *config)
{
    if (s_ctx.initialized) {
        return ESP_OK;
    }

    if (config == NULL) {
        s_ctx.config = DEFAULT_CONFIG;
    } else {
        s_ctx.config = *config;
    }

    s_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_ctx.mutex) {
        return ESP_ERR_NO_MEM;
    }

    // Install UART driver
    uart_config_t uart_cfg = {
        .baud_rate = s_ctx.config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    esp_err_t ret = uart_driver_install(s_ctx.config.uart_port,
                                        s_ctx.config.buf_size,
                                        s_ctx.config.buf_size,
                                        0, NULL, 0);
    if (ret != ESP_OK) goto err;
    ret = uart_param_config(s_ctx.config.uart_port, &uart_cfg);
    if (ret != ESP_OK) goto err;
    ret = uart_set_pin(s_ctx.config.uart_port,
                       s_ctx.config.tx_pin,
                       s_ctx.config.rx_pin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) goto err;

    s_ctx.initialized = true;

    // Allow module to power up
    vTaskDelay(pdMS_TO_TICKS(5000));
    clear_uart_buffer();

    // Basic AT test
    for (int i = 0; i < s_ctx.config.retry_count; i++) {
        if (gsm_driver_send_at("AT", "OK", 5000, NULL, 0) == ESP_OK) {
            s_ctx.module_ready = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!s_ctx.module_ready) {
        ESP_LOGE(TAG, "Module not responding");
        ret = ESP_FAIL;
        goto err;
    }

    // Configure module: echo off, text mode, new SMS indications
    gsm_driver_send_at("ATE0", "OK", 3000, NULL, 0);
    gsm_driver_send_at("AT+CMGF=1", "OK", 3000, NULL, 0);
    gsm_driver_send_at("AT+CNMI=2,1,0,0,0", "OK", 3000, NULL, 0);
    // Delete all old SMS
    gsm_driver_send_at("AT+CMGDA=\"DEL ALL\"", "OK", 10000, NULL, 0);

    ESP_LOGI(TAG, "Driver initialised, module ready");
    return ESP_OK;

err:
    if (s_ctx.mutex) vSemaphoreDelete(s_ctx.mutex);
    uart_driver_delete(s_ctx.config.uart_port);
    s_ctx.initialized = false;
    return ret;
}

esp_err_t gsm_driver_deinit(void)
{
    if (!s_ctx.initialized) return ESP_OK;
    uart_driver_delete(s_ctx.config.uart_port);
    vSemaphoreDelete(s_ctx.mutex);
    memset(&s_ctx, 0, sizeof(s_ctx));
    return ESP_OK;
}

esp_err_t gsm_driver_send_sms(const char *number, const char *message, uint32_t timeout_ms)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    // Ensure text mode
    if (gsm_driver_send_at("AT+CMGF=1", "OK", 3000, NULL, 0) != ESP_OK) {
        ret = ESP_FAIL;
        goto out;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
    // Wait for '>' prompt
    if (gsm_driver_send_at(cmd, ">", 5000, NULL, 0) != ESP_OK) {
        ret = ESP_FAIL;
        goto out;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Send message with Ctrl+Z (0x1A)
    char full_msg[256];
    snprintf(full_msg, sizeof(full_msg), "%s%c", message, 0x1A);
    uart_write_bytes(s_ctx.config.uart_port, full_msg, strlen(full_msg));
    // Wait for +CMGS: or OK
    if (wait_for_response("+CMGS:", timeout_ms, NULL, 0) != ESP_OK) {
        // Try OK as fallback
        if (wait_for_response("OK", 5000, NULL, 0) != ESP_OK) {
            ret = ESP_FAIL;
        }
    }

out:
    if (ret != ESP_OK) {
        // Attempt to clear any pending state by sending AT
        uart_write_bytes(s_ctx.config.uart_port, "AT\r", 3);
        wait_for_response("OK", 2000, NULL, 0);
        clear_uart_buffer();
    }
    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

esp_err_t parse_cmgl_response(const char *resp, uint8_t *indices, int *count, int max)
{
    // Simplified: find +CMGL: lines and extract index
    const char *p = resp;
    int cnt = 0;
    while ((p = strstr(p, "+CMGL:")) != NULL) {
        p += 6;
        int idx = atoi(p);
        if (idx > 0 && cnt < max) {
            indices[cnt++] = idx;
        }
        // skip to next
        p = strchr(p, '\n');
        if (!p) break;
        p++;
    }
    *count = cnt;
    return ESP_OK;
}

esp_err_t gsm_driver_list_sms(char *response_buffer, size_t buffer_size)
{
    return gsm_driver_send_at("AT+CMGL=\"ALL\"", "OK", 10000,
                              response_buffer, buffer_size);
}

esp_err_t gsm_driver_read_sms(uint8_t index, gsm_raw_sms_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);
    char resp[1024];
    esp_err_t ret = gsm_driver_send_at(cmd, "+CMGR:", 5000, resp, sizeof(resp));
    if (ret != ESP_OK) return ret;

    // Parse +CMGR: response (very basic)
    // Format: +CMGR: "REC READ","+255688173415","","23/11/12,15:30:25+12"\r\nmessage...
    memset(out, 0, sizeof(gsm_raw_sms_t));
    out->index = index;

    char *ptr = strstr(resp, "\",\"");
    if (ptr) {
        ptr += 3;
        char *end = strchr(ptr, '\"');
        if (end && (end - ptr) < (int)sizeof(out->sender)) {
            strncpy(out->sender, ptr, end - ptr);
        }
    }
    // Find message start (after second \r\n)
    char *msg_start = strstr(resp, "\r\n\r\n");
    if (msg_start) {
        msg_start += 4;
        char *msg_end = strstr(msg_start, "\r\nOK");
        if (!msg_end) msg_end = msg_start + strlen(msg_start);
        size_t len = msg_end - msg_start;
        if (len < sizeof(out->message)) {
            strncpy(out->message, msg_start, len);
        }
    }
    // Clean sender: keep only digits and '+'
    char cleaned[16];
    char *s = out->sender, *d = cleaned;
    while (*s) {
        if (isdigit((unsigned char)*s) || *s == '+') *d++ = *s;
        s++;
    }
    *d = '\0';
    strcpy(out->sender, cleaned);
    return ESP_OK;
}

esp_err_t gsm_driver_delete_sms(uint8_t index)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
    return gsm_driver_send_at(cmd, "OK", 5000, NULL, 0);
}

esp_err_t gsm_driver_reset(void)
{
    esp_err_t ret = gsm_driver_send_at("AT+CFUN=1,1", "OK", 10000, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10000));
    // After reset, re-init basic settings
    gsm_driver_send_at("ATE0", "OK", 3000, NULL, 0);
    gsm_driver_send_at("AT+CMGF=1", "OK", 3000, NULL, 0);
    gsm_driver_send_at("AT+CNMI=2,1,0,0,0", "OK", 3000, NULL, 0);
    return ret;
}

bool gsm_driver_is_ready(void)
{
    return s_ctx.initialized && s_ctx.module_ready;
}

// Modify wait_for_response to accept a quiet flag? Actually we'll keep as is, but logging inside send_at.
// We'll change gsm_driver_send_at_quiet to handle logging.

esp_err_t gsm_driver_send_at_quiet(const char *cmd, const char *expected,
                                   uint32_t timeout_ms, char *response, size_t resp_size,
                                   bool quiet)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        if (!quiet) ESP_LOGE(TAG, "Mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    char full_cmd[64];
    snprintf(full_cmd, sizeof(full_cmd), "%s\r", cmd);
    if (!quiet) ESP_LOGD(TAG, "TX: %s", cmd);

    clear_uart_buffer();
    uart_write_bytes(s_ctx.config.uart_port, full_cmd, strlen(full_cmd));

    esp_err_t ret = wait_for_response(expected, timeout_ms, response, resp_size);
    if (ret != ESP_OK) {
        // Flush any remaining data to clear module state
        clear_uart_buffer();
        if (!quiet) {
            if (ret == ESP_ERR_TIMEOUT)
                ESP_LOGE(TAG, "Timeout waiting for '%s'", expected);
            else
                ESP_LOGE(TAG, "Module returned ERROR");
        }
    }
    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

bool gsm_driver_ping(void)
{
    return (gsm_driver_send_at_quiet("AT", "OK", 2000, NULL, 0, true) == ESP_OK);
}

bool gsm_driver_is_registered(void)
{
    if (!s_ctx.initialized) return false;
    char resp[128];
    esp_err_t ret = gsm_driver_send_at_quiet("AT+CREG?", "+CREG:", 5000, resp, sizeof(resp), true);
    if (ret != ESP_OK) return false;
    char *p = strstr(resp, "+CREG:");
    if (!p) return false;
    p = strchr(p, ',');
    if (!p) return false;
    p++;
    int stat = atoi(p);
    return (stat == 1 || stat == 5);
}