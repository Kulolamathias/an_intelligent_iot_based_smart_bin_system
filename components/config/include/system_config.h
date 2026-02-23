/**
 * @file components/config/include/system_config.h
 * @brief System configuration constants and defaults.
 * @author Mathias Kulola
 * @date 2024-12-23
 * @version 1.0.0
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * HARDWARE PIN CONFIGURATION
 * ============================================================ */

/**
 * @brief GPIO pin assignments.
 * Modify these according to your actual hardware connections.
 */
typedef struct {
    // Sensor pins
    int pir_pin;                /**< PIR sensor output pin */
    int ultrasonic_trig_pin;    /**< Ultrasonic trigger pin */
    int ultrasonic_echo_pin;    /**< Ultrasonic echo pin */
    
    // Actuator pins
    int servo_pin;              /**< Servo motor control pin */
    int lock_servo_pin;         /**< Lock servo pin (if separate) */
    
    // Indicator pins
    int led_green_pin;          /**< Green LED pin */
    int led_yellow_pin;         /**< Yellow LED pin */
    int led_red_pin;            /**< Red LED pin */
    int buzzer_pin;             /**< Buzzer pin */
    
    // UI pins
    int lcd_sda_pin;            /**< LCD I2C SDA pin */
    int lcd_scl_pin;            /**< LCD I2C SCL pin */
    int keypad_rows[4];         /**< Keypad row pins */
    int keypad_cols[4];         /**< Keypad column pins */
} gpio_config_t;

/* ============================================================
 * SYSTEM DEFAULTS
 * ============================================================ */

// Threshold defaults
#define DEFAULT_NEAR_FULL_THRESHOLD     80    /**< 80% = early warning */
#define DEFAULT_FULL_THRESHOLD          95    /**< 95% = bin full */
#define DEFAULT_EMPTY_THRESHOLD         10    /**< 10% = bin empty */

// Timeout defaults (in milliseconds)
#define DEFAULT_INTENT_TIMEOUT_MS       5000      /**< 5 seconds for intent detection */
#define DEFAULT_LID_TIMEOUT_MS          10000     /**< 10 seconds lid open timeout */
#define DEFAULT_ESCALATION_TIMEOUT_MS   900000    /**< 15 minutes for escalation */
#define DEFAULT_PERIODIC_REPORT_MS      300000    /**< 5 minutes for periodic reports */
#define DEFAULT_HEARTBEAT_INTERVAL_MS   60000     /**< 1 minute for heartbeat */

// Network defaults
#define DEFAULT_WIFI_SSID               "SmartBin_Network"
#define DEFAULT_WIFI_PASSWORD           "smartbin123"
#define DEFAULT_MQTT_BROKER             "mqtt://broker.example.com"
#define DEFAULT_MQTT_PORT               1883
#define DEFAULT_GSM_APN                 "internet"

// Notification defaults
#define DEFAULT_ADMIN_PHONE             "+255123456789"
#define DEFAULT_OPERATOR_PHONE          "+255987654321"

/* ============================================================
 * FEATURE FLAGS
 * ============================================================ */

/**
 * @brief Feature configuration flags.
 * Enable/disable features at compile time.
 */
typedef struct {
    bool enable_gsm_notifications;      /**< Enable GSM SMS notifications */
    bool enable_wifi_dashboard;         /**< Enable WiFi web dashboard */
    bool enable_neighbor_sync;          /**< Enable inter-bin communication */
    bool enable_power_saving;           /**< Enable power saving features */
    bool enable_error_recovery;         /**< Enable automatic error recovery */
    bool enable_debug_logging;          /**< Enable verbose debug logging */
} feature_flags_t;

/* ============================================================
 * CONFIGURATION MANAGEMENT FUNCTIONS
 * ============================================================ */

/**
 * @brief Initialize system configuration from NVS.
 * @return ESP_OK on success.
 */
esp_err_t system_config_init(void);

/**
 * @brief Save current configuration to NVS.
 * @return ESP_OK on success.
 */
esp_err_t system_config_save(void);

/**
 * @brief Restore configuration to factory defaults.
 * @return ESP_OK on success.
 */
esp_err_t system_config_restore_defaults(void);

/**
 * @brief Get GPIO configuration.
 * @return Pointer to GPIO configuration.
 */
const gpio_config_t* system_config_get_gpio(void);

/**
 * @brief Get feature flags.
 * @return Pointer to feature flags.
 */
const feature_flags_t* system_config_get_features(void);

#endif /* SYSTEM_CONFIG_H */