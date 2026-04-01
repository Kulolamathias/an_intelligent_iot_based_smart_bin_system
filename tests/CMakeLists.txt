idf_component_register(
    SRCS 
        # "src/test_harness.c"
        "src/ultrasonic_test.c"
        "src/wifi_mqtt_test.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        core 
        services

        efuse
)