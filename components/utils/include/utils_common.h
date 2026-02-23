/**
 * @file components/utils/include/utils_common.h
 * @brief Common utility functions and macros.
 * @author Mathias Kulola
 * @date 2024-12-23
 * @version 1.0.0
 */

#ifndef UTILS_COMMON_H
#define UTILS_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"



 /* ============================================================
 * LOGGING MACROS
 * ============================================================ */

/**
 * @brief Tagged logging macro with module prefix.
 */
#define LOG_TAG(module) #module

/**
 * @brief Log error with return.
 */
#define LOG_ERROR_RETURN(tag, format, ...) \
    do { \
        ESP_LOGE(tag, format, ##__VA_ARGS__); \
        return ESP_FAIL; \
    } while(0)

/**
 * @brief Log warning with return.
 */
#define LOG_WARN_RETURN(tag, format, ...) \
    do { \
        ESP_LOGW(tag, format, ##__VA_ARGS__); \
        return ESP_FAIL; \
    } while(0)

/**
 * @brief Log and return error code.
 */
#define LOG_ERRCODE_RETURN(tag, err, format, ...) \
    do { \
        ESP_LOGE(tag, "%s (err: 0x%x)", format, ##__VA_ARGS__, err); \
        return err; \
    } while(0)


/* ============================================================
 * MEMORY MANAGEMENT
 * ============================================================ */

/**
 * @brief Allocate memory with error checking.
 */
#define MALLOC_CHECK(size) \
    ({ \
        void *ptr = malloc(size); \
        if (ptr == NULL) { \
            ESP_LOGE("MEM", "Failed to allocate %d bytes", (int)(size)); \
        } \
        ptr; \
    })

/**
 * @brief Allocate and zero memory.
 */
#define CALLOC_CHECK(count, size) \
    ({ \
        void *ptr = calloc(count, size); \
        if (ptr == NULL) { \
            ESP_LOGE("MEM", "Failed to allocate %d bytes", (int)((count)*(size))); \
        } \
        ptr; \
    })

/**
 * @brief Free memory if not NULL.
 */
#define FREE_IF_NOT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            free(ptr); \
            (ptr) = NULL; \
        } \
    } while(0)

/* ============================================================
 * DATA STRUCTURE HELPERS
 * ============================================================ */

/**
 * @brief Circular buffer structure.
 */
typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
} circular_buffer_t;

/**
 * @brief Initialize a circular buffer.
 */
esp_err_t circular_buffer_init(circular_buffer_t *cb, size_t capacity);

/**
 * @brief Push data to circular buffer.
 */
esp_err_t circular_buffer_push(circular_buffer_t *cb, uint8_t data);

/**
 * @brief Pop data from circular buffer.
 */
esp_err_t circular_buffer_pop(circular_buffer_t *cb, uint8_t *data);

/**
 * @brief Check if buffer is empty.
 */
bool circular_buffer_is_empty(circular_buffer_t *cb);

/**
 * @brief Check if buffer is full.
 */
bool circular_buffer_is_full(circular_buffer_t *cb);

/* ============================================================
 * STRING UTILITIES
 * ============================================================ */

/**
 * @brief Safe string copy (guaranteed null termination).
 */
size_t strncpy_safe(char *dest, const char *src, size_t dest_size);

/**
 * @brief Convert integer to string.
 */
char* itoa_safe(int value, char *buffer, int base);

/**
 * @brief Convert float to string with precision.
 */
char* ftoa_safe(float value, char *buffer, int precision);

/* ============================================================
 * MATH UTILITIES
 * ============================================================ */

/**
 * @brief Constrain value between min and max.
 */
int32_t constrain_int32(int32_t value, int32_t min, int32_t max);

/**
 * @brief Constrain value between min and max.
 */
float constrain_float(float value, float min, float max);

/**
 * @brief Map value from one range to another.
 */
int32_t map_int32(int32_t value, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max);

/**
 * @brief Map value from one range to another.
 */
float map_float(float value, float in_min, float in_max, float out_min, float out_max);

/* ============================================================
 * TIME UTILITIES
 * ============================================================ */

/**
 * @brief Get current timestamp in milliseconds.
 */
uint32_t time_get_ms(void);

/**
 * @brief Check if timeout has expired.
 */
bool timeout_expired(uint32_t start_time, uint32_t timeout_ms);

/**
 * @brief Calculate elapsed time.
 */
uint32_t time_elapsed_ms(uint32_t start_time);

/**
 * @brief Non-blocking delay that checks condition.
 */
esp_err_t delay_while(uint32_t timeout_ms, bool (*condition)(void));

#endif /* UTILS_COMMON_H */