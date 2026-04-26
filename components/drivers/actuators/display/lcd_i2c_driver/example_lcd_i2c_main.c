
#if 0

/**
 * @file main.c
 * @brief Corrected I2C scanner – sends a single byte to each address.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "I2C_SCAN";

#define I2C_PORT 0
#define I2C_SDA  GPIO_NUM_21
#define I2C_SCL  GPIO_NUM_22
#define I2C_FREQ 50000

void app_main(void)
{
    ESP_LOGI(TAG, "Initialising I2C master bus...");

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_APB,
        .glitch_ignore_cnt = 10,
        .flags = {
            .enable_internal_pullup = false,
        },
    };
    i2c_master_bus_handle_t bus;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bus init failed: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "Scanning addresses 0x01-0x7F...");
    uint8_t test_byte = 0x00;

    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = I2C_FREQ,
        };
        i2c_master_dev_handle_t dev;
        ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
        if (ret == ESP_OK) {
            ret = i2c_master_transmit(dev, &test_byte, 1, pdMS_TO_TICKS(100));
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Found device at 0x%02X", addr);
            }
            i2c_master_bus_rm_device(dev);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "Scan complete.");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}



#else



/**
 * @file main.c
 * @brief Simplified LCD driver test – single task, sequential.
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * Tests LCD driver without concurrent tasks. Each test runs one after another.
 * Use this to isolate whether garbled text is caused by concurrency or by
 * driver/hardware issues.
 *
 * =============================================================================
 * @author SoY. Mathithyahu
 * @date 2026/04/07
 * =============================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "lcd_i2c_driver.h"

static const char *TAG = "LCD_TEST";

/* I2C configuration – adjust to your wiring */
#define I2C_MASTER_NUM    0
#define I2C_MASTER_SDA    GPIO_NUM_21
#define I2C_MASTER_SCL    GPIO_NUM_22
#define I2C_MASTER_FREQ   50000          /* 50 kHz – more reliable */
#define LCD_I2C_ADDR      0x27
#define LCD_COLS          20
#define LCD_ROWS          4

static lcd_handle_t s_lcd = NULL;

/* Helper: small delay between tests */
static void wait_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ============================================================================
 * Test 1: Basic write and printf
 * ============================================================================ */
static void test_basic(void)
{
    ESP_LOGI(TAG, "--- Test 1: Basic write & printf ---");
    lcd_driver_clear(s_lcd);
    wait_ms(100);
    lcd_driver_write_string(s_lcd, 0, 0, "Hello LCD!");
    wait_ms(1000);
    lcd_driver_printf(s_lcd, 1, 0, "Cols=%d Rows=%d", LCD_COLS, LCD_ROWS);
    wait_ms(1000);
    lcd_driver_printf(s_lcd, 2, 0, "Hex: 0x%04X", 0xABCD);
    wait_ms(1000);
    lcd_driver_printf(s_lcd, 3, 0, "Num: %d", 12345);
    wait_ms(2000);
}

/* ============================================================================
 * Test 2: Scrolling left and right
 * ============================================================================ */
static void test_scroll(void)
{
    ESP_LOGI(TAG, "--- Test 2: Scrolling ---");
    lcd_driver_clear(s_lcd);
    lcd_driver_write_string(s_lcd, 1, 0, "Scrolling text...");
    wait_ms(1000);

    for (int i = 0; i < 8; i++) {
        lcd_driver_shift_left(s_lcd);
        wait_ms(300);
    }
    for (int i = 0; i < 8; i++) {
        lcd_driver_shift_right(s_lcd);
        wait_ms(300);
    }
    wait_ms(1000);
}

/* ============================================================================
 * Test 3: Backlight toggle
 * ============================================================================ */
static void test_backlight(void)
{
    ESP_LOGI(TAG, "--- Test 3: Backlight toggle ---");
    for (int i = 0; i < 3; i++) {
        lcd_driver_backlight(s_lcd, false);
        wait_ms(500);
        lcd_driver_backlight(s_lcd, true);
        wait_ms(500);
    }
}

/* ============================================================================
 * Test 4: Cursor movement and home
 * ============================================================================ */
static void test_cursor(void)
{
    ESP_LOGI(TAG, "--- Test 4: Cursor & home ---");
    lcd_driver_clear(s_lcd);
    lcd_driver_set_cursor(s_lcd, 0, 0);
    lcd_driver_write_string(s_lcd, 0, 0, "Cursor test");
    wait_ms(1000);
    lcd_driver_home(s_lcd);
    lcd_driver_write_string(s_lcd, 0, 0, "Home!          ");
    wait_ms(1000);
}

/* ============================================================================
 * Test 5: String truncation (no overflow)
 * ============================================================================ */
static void test_truncation(void)
{
    ESP_LOGI(TAG, "--- Test 5: String truncation ---");
    lcd_driver_clear(s_lcd);
    const char *long_str = "This is a very long string that definitely exceeds LCD width";
    lcd_driver_write_string(s_lcd, 0, 0, long_str);
    wait_ms(2000);
}

/* ============================================================================
 * Main entry point
 * ============================================================================ */
void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initialising I2C master bus...");

    /* Create I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA,
        .scl_io_num = I2C_MASTER_SCL,
        .clk_source = I2C_CLK_SRC_APB,
        .glitch_ignore_cnt = 10,
        .flags = {
            .enable_internal_pullup = false,
        },
    };
    i2c_master_bus_handle_t bus_handle;
    ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %d", ret);
        return;
    }

    /* Initialise LCD driver */
    lcd_config_t lcd_cfg = {
        .i2c_bus = bus_handle,
        .i2c_addr = LCD_I2C_ADDR,
        .cols = LCD_COLS,
        .rows = LCD_ROWS,
        .backlight_on_init = true,
    };
    ret = lcd_driver_create(&lcd_cfg, &s_lcd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD driver init failed: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "LCD test started (single task, sequential)");

    /* Run tests sequentially */
    test_basic();
    test_scroll();
    test_backlight();
    test_cursor();
    test_truncation();

    /* Final message */
    lcd_driver_clear(s_lcd);
    lcd_driver_write_string(s_lcd, 1, 0, "Test completed.");
    ESP_LOGI(TAG, "All tests done. LCD will idle.");

    /* Idle forever */
    while (1) {
        wait_ms(1000);
    }
}






#endif
