/**
 * @file components/services/actuation/lcd_service/include/lcd_service.h
 * @brief LCD Service – command interface for LCD display.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service receives commands from the command router and executes them
 * using the LCD driver. It owns the LCD handle and ensures thread‑safe access.
 *
 * Responsibilities:
 *   - Initialise the LCD driver (I2C, pins, dimensions).
 *   - Register command handlers for LCD operations.
 *   - Execute commands: show message, clear, backlight, scroll, cursor.
 *
 * Forbidden:
 *   - No event posting (pure actuation).
 *   - No business logic or state decisions.
 * =============================================================================
 * @author SoY. Mathithyahu
 * @date 2026/04/07
 * =============================================================================
 */

#ifndef LCD_SERVICE_H
#define LCD_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the LCD service.
 *
 * Creates the I2C master bus, initialises the LCD driver,
 * and prepares internal shadow buffer.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t lcd_service_init(void);

/**
 * @brief Register LCD command handlers with the command router.
 *
 * Handles:
 *   - CMD_SHOW_MESSAGE
 *   - CMD_CLEAR_LCD
 *   - CMD_SET_BACKLIGHT
 *   - CMD_LCD_CURSOR (optional)
 *   - CMD_LCD_SCROLL_LEFT / RIGHT (optional)
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t lcd_service_register_handlers(void);

/**
 * @brief Start the LCD service.
 *
 * Turns on backlight and clears display. No background tasks.
 *
 * @return ESP_OK always.
 */
esp_err_t lcd_service_start(void);

/**
 * @brief Stop the LCD service (turn off backlight, delete driver).
 *
 * @return ESP_OK always.
 */
esp_err_t lcd_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* LCD_SERVICE_H */