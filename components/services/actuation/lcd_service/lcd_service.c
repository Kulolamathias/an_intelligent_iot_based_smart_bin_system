/**
 * @file components/services/actuation/lcd_service/lcd_service.c
 * @brief LCD Service – implementation.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements command handlers for LCD control using the LCD driver.
 * Commands are executed synchronously in the command router's context.
 * A mutex protects against any potential concurrent access (safety net).
 * =============================================================================
 * @author SoY. Mathithyahu
 * @date 2026/04/07
 * =============================================================================
 */

#include "lcd_service.h"
#include "lcd_i2c_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "command_types.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "LCD_SVC";

/* ============================================================================
 * Configuration (move to Kconfig later)
 * ============================================================================ */
#define I2C_MASTER_PORT     0
#define I2C_SDA_PIN         GPIO_NUM_21
#define I2C_SCL_PIN         GPIO_NUM_22
#define I2C_FREQ_HZ         20000          /* 20 kHz from 50 kHz */
#define LCD_I2C_ADDR        0x27
#define LCD_COLS            20
#define LCD_ROWS            4

/* ============================================================================
 * Static variables
 * ============================================================================ */
static lcd_handle_t s_lcd = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static SemaphoreHandle_t s_mutex = NULL;

/* ============================================================================
 * Helper: ensure driver initialised
 * ============================================================================ */
static bool is_initialised(void)
{
    return (s_lcd != NULL);
}

/* ============================================================================
 * Command Handlers (called from command router)
 * ============================================================================ */

/**
 * @brief CMD_SHOW_MESSAGE – display up to 2 lines.
 */
static esp_err_t cmd_show_message(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    if (!is_initialised()) return ESP_ERR_INVALID_STATE;

    const cmd_show_message_params_t *p = &params->show_message;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    esp_err_t ret = ESP_OK;
    if (p->line1[0] != '\0') {
        ret = lcd_driver_write_string(s_lcd, 0, 0, p->line1);
    }
    if (ret == ESP_OK && p->line2[0] != '\0') {
        ret = lcd_driver_write_string(s_lcd, 1, 0, p->line2);
    }

    xSemaphoreGive(s_mutex);
    return ret;
}

/**
 * @brief CMD_CLEAR_LCD – clear display and home cursor.
 */
static esp_err_t cmd_clear_lcd(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!is_initialised()) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = lcd_driver_clear(s_lcd);
    xSemaphoreGive(s_mutex);
    return ret;
}

/**
 * @brief CMD_SET_BACKLIGHT – turn backlight on/off.
 */
static esp_err_t cmd_set_backlight(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    if (!is_initialised()) return ESP_ERR_INVALID_STATE;

    const cmd_backlight_params_t *p = &params->backlight;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = lcd_driver_backlight(s_lcd, p->on);
    xSemaphoreGive(s_mutex);
    return ret;
}

/**
 * @brief CMD_LCD_CURSOR – set cursor position.
 */
static esp_err_t cmd_lcd_cursor(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    if (!is_initialised()) return ESP_ERR_INVALID_STATE;

    const cmd_cursor_params_t *p = &params->cursor;

    if (p->line >= LCD_ROWS || p->col >= LCD_COLS) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = lcd_driver_set_cursor(s_lcd, p->line, p->col);
    xSemaphoreGive(s_mutex);
    return ret;
}

/**
 * @brief CMD_LCD_SCROLL_LEFT – shift display left.
 */
static esp_err_t cmd_scroll_left(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!is_initialised()) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = lcd_driver_shift_left(s_lcd);
    xSemaphoreGive(s_mutex);
    return ret;
}

/**
 * @brief CMD_LCD_SCROLL_RIGHT – shift display right.
 */
static esp_err_t cmd_scroll_right(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (!is_initialised()) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = lcd_driver_shift_right(s_lcd);
    xSemaphoreGive(s_mutex);
    return ret;
}

/* ============================================================================
 * Wrappers for command router (void* params)
 * ============================================================================ */
static esp_err_t show_message_wrapper(void *context, void *params)
{
    return cmd_show_message(context, (const command_param_union_t*)params);
}

static esp_err_t clear_lcd_wrapper(void *context, void *params)
{
    return cmd_clear_lcd(context, (const command_param_union_t*)params);
}

static esp_err_t backlight_wrapper(void *context, void *params)
{
    return cmd_set_backlight(context, (const command_param_union_t*)params);
}

static esp_err_t cursor_wrapper(void *context, void *params)
{
    return cmd_lcd_cursor(context, (const command_param_union_t*)params);
}

static esp_err_t scroll_left_wrapper(void *context, void *params)
{
    return cmd_scroll_left(context, (const command_param_union_t*)params);
}

static esp_err_t scroll_right_wrapper(void *context, void *params)
{
    return cmd_scroll_right(context, (const command_param_union_t*)params);
}

/* ============================================================================
 * Public API – Service Manager Lifecycle
 * ============================================================================ */

esp_err_t lcd_service_init(void)
{
    if (s_lcd != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Create mutex */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Create I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MASTER_PORT,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_APB,
        .glitch_ignore_cnt = 10,
        .flags = {
            .enable_internal_pullup = false,
        },
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %d", ret);
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ret;
    }

    /* Create LCD driver */
    lcd_config_t lcd_cfg = {
        .i2c_bus = s_i2c_bus,
        .i2c_addr = LCD_I2C_ADDR,
        .cols = LCD_COLS,
        .rows = LCD_ROWS,
        .backlight_on_init = true,
    };
    ret = lcd_driver_create(&lcd_cfg, &s_lcd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD driver create failed: %d", ret);
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "LCD service initialised (%dx%d)", LCD_COLS, LCD_ROWS);
    return ESP_OK;
}

esp_err_t lcd_service_register_handlers(void)
{
    esp_err_t ret;

    ret = service_register_command(CMD_SHOW_MESSAGE, show_message_wrapper, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_CLEAR_LCD, clear_lcd_wrapper, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_SET_BACKLIGHT, backlight_wrapper, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_LCD_CURSOR, cursor_wrapper, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_LCD_SCROLL_LEFT, scroll_left_wrapper, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(CMD_LCD_SCROLL_RIGHT, scroll_right_wrapper, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "LCD command handlers registered");
    return ESP_OK;
}

esp_err_t lcd_service_start(void)
{
    /* Already initialised; ensure backlight on and display clear */
    if (!is_initialised()) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    lcd_driver_backlight(s_lcd, true);
    lcd_driver_clear(s_lcd);
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "LCD service started");
    return ESP_OK;
}

esp_err_t lcd_service_stop(void)
{
    if (!is_initialised()) return ESP_OK;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    lcd_driver_backlight(s_lcd, false);
    lcd_driver_clear(s_lcd);
    lcd_driver_delete(s_lcd);
    s_lcd = NULL;
    xSemaphoreGive(s_mutex);

    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;

    ESP_LOGI(TAG, "LCD service stopped");
    return ESP_OK;
}