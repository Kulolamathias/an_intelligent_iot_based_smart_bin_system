/**
 * @file components/drivers/display/lcd_i2c_driver/include/lcd_i2cdriver.h
 * @brief LCD driver for HD44780 over I2C (PCF8574).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to an I2C‑backpack LCD.
 * It abstracts the HD44780 protocol (4‑bit mode) and I2C communication.
 *
 * Responsibilities:
 *   - Initialise the LCD (timing, function set, display on/off, clear).
 *   - Write strings at specified cursor positions.
 *   - Clear display, home cursor, control backlight, scroll.
 *   - Support different column/row sizes (16x2, 20x4, etc.).
 *
 * Forbidden:
 *   - No business logic, no command handling, no event posting.
 *   - No knowledge of core, services, or state machine.
 *
 * =============================================================================
 * @author SoY. Mathithyahu
 * @date 2026/04/07
 * =============================================================================
 */

#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle representing an LCD instance.
 */
typedef struct lcd_handle_t *lcd_handle_t;

/**
 * @brief LCD configuration structure.
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;  /**< I2C master bus handle (already initialised) */
    uint8_t i2c_addr;                 /**< I2C address of the LCD backpack (e.g., 0x27) */
    uint8_t cols;                     /**< Number of columns (16, 20) */
    uint8_t rows;                     /**< Number of rows (2, 4) */
    bool backlight_on_init;           /**< If true, turn backlight on during init */
} lcd_config_t;

/**
 * @brief Create and initialise an LCD instance.
 *
 * This function initialises the I2C device, performs the HD44780 initialisation
 * sequence, clears the display, and sets the cursor to home.
 *
 * @param config Pointer to configuration structure.
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t lcd_driver_create(const lcd_config_t *config, lcd_handle_t *out_handle);

/**
 * @brief Write a formatted string at the specified cursor position.
 *
 * Supports standard printf format specifiers (%d, %x, %s, etc.). The string is
 * truncated to fit within the LCD column width from the starting column.
 *
 * @param handle LCD instance handle.
 * @param line Line index (0‑based, must be < rows).
 * @param col Column index (0‑based, must be < cols).
 * @param format printf‑style format string.
 * @param ... Variable arguments for format.
 * @return ESP_OK on success, error otherwise (e.g., invalid line/col, I2C failure).
 */
esp_err_t lcd_driver_printf(lcd_handle_t handle, uint8_t line, uint8_t col,
                            const char *format, ...) __attribute__((format(printf, 4, 5)));

/**
 * @brief Write a raw string at the specified cursor position (no formatting).
 *
 * @param handle LCD instance handle.
 * @param line Line index (0‑based).
 * @param col Column index (0‑based).
 * @param str Null‑terminated string.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t lcd_driver_write_string(lcd_handle_t handle, uint8_t line, uint8_t col,
                                  const char *str);

/**
 * @brief Clear the entire display and return cursor to home (0,0).
 *
 * @param handle LCD instance handle.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t lcd_driver_clear(lcd_handle_t handle);

/**
 * @brief Return cursor to home (0,0) without clearing display.
 *
 * @param handle LCD instance handle.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t lcd_driver_home(lcd_handle_t handle);

/**
 * @brief Turn backlight on or off.
 *
 * @param handle LCD instance handle.
 * @param on true = on, false = off.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t lcd_driver_backlight(lcd_handle_t handle, bool on);

/**
 * @brief Shift the entire display left by one column (without moving cursor).
 *
 * @param handle LCD instance handle.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t lcd_driver_shift_left(lcd_handle_t handle);

/**
 * @brief Shift the entire display right by one column.
 *
 * @param handle LCD instance handle.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t lcd_driver_shift_right(lcd_handle_t handle);

/**
 * @brief Set the cursor position (line, col) for subsequent writes.
 *
 * @param handle LCD instance handle.
 * @param line Line index (0‑based).
 * @param col Column index (0‑based).
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t lcd_driver_set_cursor(lcd_handle_t handle, uint8_t line, uint8_t col);

/**
 * @brief Delete an LCD instance and free resources.
 *
 * @param handle LCD instance handle.
 * @return ESP_OK on success.
 */
esp_err_t lcd_driver_delete(lcd_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* LCD_DRIVER_H */