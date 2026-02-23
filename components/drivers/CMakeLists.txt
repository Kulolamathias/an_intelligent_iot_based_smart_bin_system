#@file components/drivers/CMakeLists.txt
#@brief CMake file for drivers components

idf_component_register(
    SRCS
        # "sensors/pir/pir_driver.c"
        "sensors/ultrasonic_driver/ultrasonic_driver.c"
        "actuators/servo_driver/servo_driver.c"
        "actuators/lcd_driver/lcd_driver.c"
        "actuators/led_driver/led_driver.c"
        "actuators/buzzer_driver/buzzer_driver.c"

        "comms/gsm_sim800/gsm_sim800_driver.c"

    INCLUDE_DIRS
        "sensors/pir"
        "sensors/ultrasonic_driver"

        "actuators/led_driver"
        "actuators/lcd_driver"
        "actuators/servo_driver"
        "actuators/buzzer_driver"

        "comms/gsm_sim800"

    REQUIRES
        utils
        driver
        freertos
        esp_timer       
)