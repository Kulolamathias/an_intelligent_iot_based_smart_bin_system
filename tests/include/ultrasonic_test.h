/**
 * @file tests/include/ultrasonic_test.h
 * @brief Ultrasonic sensor test – runs two sensors.
 */

#ifndef ULTRASONIC_TEST_H
#define ULTRASONIC_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ultrasonic_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* ULTRASONIC_TEST_H */