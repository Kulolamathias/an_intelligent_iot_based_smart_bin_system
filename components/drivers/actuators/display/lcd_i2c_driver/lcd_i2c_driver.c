/**
 * @file components/drivers/display/lcd_i2c/lcd_driver.c
 * @brief LCD driver for HD44780 over I2C (PCF8574) – robust implementation.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements HD44780 4‑bit protocol with correct timing and error recovery.
 *
 * =============================================================================
 * @author SoY. Mathithyahu
 * @date 2026/04/07
 * =============================================================================
 */

#include "lcd_i2c_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "LCD_DRV";

/* HD44780 commands */
#define LCD_CMD_CLEAR        0x01
#define LCD_CMD_HOME         0x02
#define LCD_CMD_ENTRY_MODE   0x06   /* increment, no shift */
#define LCD_CMD_DISP_ON      0x0C   /* display on, cursor off, blink off */
#define LCD_CMD_FUNCTION_4BIT 0x28  /* 4‑bit, 2 lines, 5x8 dots */
#define LCD_CMD_SET_CURSOR   0x80

/* I2C backpack PCF8574 bits */
#define LCD_BIT_RS           0x01
#define LCD_BIT_RW           0x02   /* unused, always 0 */
#define LCD_BIT_EN           0x04
#define LCD_BIT_BACKLIGHT    0x08
#define LCD_BIT_DATA4        0x10
#define LCD_BIT_DATA5        0x20
#define LCD_BIT_DATA6        0x40
#define LCD_BIT_DATA7        0x80

/* Timing (microseconds) */
#define PULSE_WIDTH_US       2      /* EN high pulse width (min 450 ns) */
#define NIBBLE_DELAY_US      100    /* delay between high and low nibble */
#define CMD_DELAY_MS         2      /* after most commands (except clear/home) */
#define CLEAR_HOME_DELAY_MS  2      /* clear and home need >1.5 ms */

/* I2C */
#define I2C_TIMEOUT_MS       100
#define I2C_RETRY_COUNT      3

struct lcd_handle_t {
    i2c_master_dev_handle_t i2c_dev;
    SemaphoreHandle_t mutex;
    uint8_t cols;
    uint8_t rows;
    uint8_t backlight_mask;
};

/**
 * @brief Send a single I2C byte (with EN pulse generation).
 *
 * This function sends one byte to the PCF8574, with EN high for a short pulse.
 * The byte already contains the EN bit set to 1 or 0 accordingly.
 */
static esp_err_t i2c_write_byte(lcd_handle_t handle, uint8_t data)
{
    esp_err_t ret;
    for (int attempt = 0; attempt < I2C_RETRY_COUNT; attempt++) {
        ret = i2c_master_transmit(handle->i2c_dev, &data, 1, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
        if (ret == ESP_OK) break;
        ESP_LOGW(TAG, "I2C transmit attempt %d failed: %d", attempt + 1, ret);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ret;
}

/**
 * @brief Send a nibble (4 bits) with EN pulse.
 *
 * @param handle LCD handle.
 * @param nibble Lower 4 bits contain the data (bits 4-7 are ignored).
 * @param rs Register select (0=command, 1=data).
 */
static esp_err_t lcd_send_nibble(lcd_handle_t handle, uint8_t nibble, bool rs)
{
    uint8_t byte = (nibble & 0xF0) | handle->backlight_mask | (rs ? LCD_BIT_RS : 0);

    /* Send EN high */
    esp_err_t ret = i2c_write_byte(handle, byte | LCD_BIT_EN);
    if (ret != ESP_OK) return ret;
    esp_rom_delay_us(PULSE_WIDTH_US);

    /* Send EN low */
    ret = i2c_write_byte(handle, byte);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

/**
 * @brief Send a full byte (command or data) using two nibbles.
 */
static esp_err_t lcd_send_byte(lcd_handle_t handle, uint8_t data, bool rs)
{
    uint8_t high_nibble = data & 0xF0;
    uint8_t low_nibble  = (data << 4) & 0xF0;

    esp_err_t ret = lcd_send_nibble(handle, high_nibble, rs);
    if (ret != ESP_OK) return ret;
    esp_rom_delay_us(NIBBLE_DELAY_US);

    ret = lcd_send_nibble(handle, low_nibble, rs);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

static esp_err_t lcd_send_cmd(lcd_handle_t handle, uint8_t cmd)
{
    return lcd_send_byte(handle, cmd, false);
}

static esp_err_t lcd_send_data(lcd_handle_t handle, uint8_t data)
{
    return lcd_send_byte(handle, data, true);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

esp_err_t lcd_driver_create(const lcd_config_t *config, lcd_handle_t *out_handle)
{
    if (!config || !out_handle || !config->i2c_bus) return ESP_ERR_INVALID_ARG;
    if (config->cols == 0 || config->rows == 0) return ESP_ERR_INVALID_ARG;

    lcd_handle_t handle = calloc(1, sizeof(struct lcd_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->cols = config->cols;
    handle->rows = config->rows;
    handle->backlight_mask = config->backlight_on_init ? LCD_BIT_BACKLIGHT : 0;

    handle->mutex = xSemaphoreCreateMutex();
    if (!handle->mutex) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    /* Add I2C device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(config->i2c_bus, &dev_cfg, &handle->i2c_dev);
    if (ret != ESP_OK) {
        vSemaphoreDelete(handle->mutex);
        free(handle);
        return ret;
    }

    xSemaphoreTake(handle->mutex, portMAX_DELAY);

    /* Wait for LCD to power up */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Initialisation sequence (4‑bit mode) */
    uint8_t init_cmds[] = {0x30, 0x30, 0x20};  /* 8‑bit twice, then 4‑bit */
    for (int i = 0; i < 3; i++) {
        ret = lcd_send_nibble(handle, init_cmds[i], false);
        if (ret != ESP_OK) goto cleanup;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Function set (4‑bit, 2 lines, 5x8) */
    ret = lcd_send_cmd(handle, LCD_CMD_FUNCTION_4BIT);
    if (ret != ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    /* Display on/off control */
    ret = lcd_send_cmd(handle, LCD_CMD_DISP_ON);
    if (ret != ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    /* Clear display */
    ret = lcd_send_cmd(handle, LCD_CMD_CLEAR);
    if (ret != ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(CLEAR_HOME_DELAY_MS));

    /* Entry mode set */
    ret = lcd_send_cmd(handle, LCD_CMD_ENTRY_MODE);
    if (ret != ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    xSemaphoreGive(handle->mutex);
    *out_handle = handle;
    ESP_LOGI(TAG, "LCD driver initialised: %dx%d, addr 0x%02X", handle->cols, handle->rows, config->i2c_addr);
    return ESP_OK;

cleanup:
    xSemaphoreGive(handle->mutex);
    i2c_master_bus_rm_device(handle->i2c_dev);
    vSemaphoreDelete(handle->mutex);
    free(handle);
    return ret;
}

esp_err_t lcd_driver_printf(lcd_handle_t handle, uint8_t line, uint8_t col,
                            const char *format, ...)
{
    if (!handle || !format) return ESP_ERR_INVALID_ARG;
    if (line >= handle->rows || col >= handle->cols) return ESP_ERR_INVALID_ARG;

    va_list args;
    va_start(args, format);
    char buffer[handle->cols + 1];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    return lcd_driver_write_string(handle, line, col, buffer);
}

esp_err_t lcd_driver_write_string(lcd_handle_t handle, uint8_t line, uint8_t col, const char *str)
{
    if (!handle || !str) return ESP_ERR_INVALID_ARG;
    if (line >= handle->rows || col >= handle->cols) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(handle->mutex, portMAX_DELAY);

    /* Set cursor */
    uint8_t pos = 0x40 * line + col;
    esp_err_t ret = lcd_send_cmd(handle, LCD_CMD_SET_CURSOR | pos);
    if (ret != ESP_OK) {
        xSemaphoreGive(handle->mutex);
        return ret;
    }

    /* Write string, truncate */
    size_t max_len = handle->cols - col;
    for (size_t i = 0; i < max_len && str[i] != '\0'; i++) {
        ret = lcd_send_data(handle, (uint8_t)str[i]);
        if (ret != ESP_OK) break;
    }

    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t lcd_driver_clear(lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    esp_err_t ret = lcd_send_cmd(handle, LCD_CMD_CLEAR);
    xSemaphoreGive(handle->mutex);
    if (ret == ESP_OK) vTaskDelay(pdMS_TO_TICKS(CLEAR_HOME_DELAY_MS));
    return ret;
}

esp_err_t lcd_driver_home(lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    esp_err_t ret = lcd_send_cmd(handle, LCD_CMD_HOME);
    xSemaphoreGive(handle->mutex);
    if (ret == ESP_OK) vTaskDelay(pdMS_TO_TICKS(CLEAR_HOME_DELAY_MS));
    return ret;
}

esp_err_t lcd_driver_backlight(lcd_handle_t handle, bool on)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    if (on) {
        handle->backlight_mask = LCD_BIT_BACKLIGHT;
    } else {
        handle->backlight_mask = 0;
    }
    /* Send a dummy byte to update backlight state */
    uint8_t dummy = handle->backlight_mask;
    esp_err_t ret = i2c_write_byte(handle, dummy);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t lcd_driver_shift_left(lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    esp_err_t ret = lcd_send_cmd(handle, 0x18);  /* shift display left */
    xSemaphoreGive(handle->mutex);
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));
    return ret;
}

esp_err_t lcd_driver_shift_right(lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    esp_err_t ret = lcd_send_cmd(handle, 0x1C);  /* shift display right */
    xSemaphoreGive(handle->mutex);
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));
    return ret;
}

esp_err_t lcd_driver_set_cursor(lcd_handle_t handle, uint8_t line, uint8_t col)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (line >= handle->rows || col >= handle->cols) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    uint8_t pos = 0x40 * line + col;
    esp_err_t ret = lcd_send_cmd(handle, LCD_CMD_SET_CURSOR | pos);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t lcd_driver_delete(lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    lcd_driver_clear(handle);
    lcd_driver_backlight(handle, false);
    xSemaphoreGive(handle->mutex);

    i2c_master_bus_rm_device(handle->i2c_dev);
    vSemaphoreDelete(handle->mutex);
    free(handle);
    ESP_LOGI(TAG, "LCD driver deleted");
    return ESP_OK;
}