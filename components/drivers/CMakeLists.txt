#@file components/drivers/CMakeLists.txt
#@brief CMake file for drivers components

idf_component_register(
    SRCS
        "sensors/pir_driver/pir_driver.c"
        "sensors/ultrasonic_driver/ultrasonic_driver.c"

        # "actuators/display/lcd_i2c_driver/example_lcd_i2c_main.c"
        "actuators/display/lcd_i2c_driver/lcd_i2c_driver.c"
        "actuators/led_driver/led_driver.c"
        "actuators/buzzer_driver/buzzer_driver.c"
        "actuators/servo_driver/servo_driver.c"

        "comms/gsm_sim800/gsm_sim800_driver.c"
        "comms/gps/gps_driver.c"

    INCLUDE_DIRS
        "sensors/pir_driver"
        "sensors/ultrasonic_driver"

        "actuators/display/lcd_i2c_driver"
        "actuators/buzzer_driver"
        "actuators/servo_driver"
        "actuators/led_driver"

        "comms/gsm_sim800"
        "comms/gps"

    REQUIRES
        common_utils
        driver
        freertos
        esp_timer       
)