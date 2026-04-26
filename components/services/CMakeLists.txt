idf_component_register(
    SRCS
        "src/service_manager.c"
        "timer/timer_service.c"

        "connectivity/connectivity_service.c"
        "connectivity/gsm/gsm_service.c"
        "connectivity/wifi/src/wifi_driver_abstraction.c"
        "connectivity/wifi/src/wifi_service.c"
        "connectivity/mqtt/src/mqtt_client_abstraction.c"
        "connectivity/mqtt/src/mqtt_service.c"
        "connectivity/mqtt/src/mqtt_topic.c"

        "networking/bin_network_service/bin_network_service.c"

        "web_command/web_command_service.c"

        "location/gps_service.c"

        "sensing/pir_service/pir_service.c"
        "sensing/ultrasonic_service/ultrasonic_service.c"

        "actuation/led_service/led_service.c"
        "actuation/buzzer_service/buzzer_service.c"
        "actuation/lcd_service/lcd_service.c"
        "actuation/servo_service/servo_service.c"

        "example/example_service.c"


    INCLUDE_DIRS 
        "include"
        "auth"
        "fault"
        "power"
        "timer"
        "config"

        "sensing"
        "sensing/pir_service"
        "sensing/ultrasonic_service"

        "actuation"
        "actuation/led_service"
        "actuation/buzzer_service"
        "actuation/lcd_service"
        "actuation/servo_service"

        "connectivity"
        "connectivity/gsm"
        "connectivity/wifi/include"
        "connectivity/mqtt/include"

        "networking/bin_network_service"

        "web_command"

        "indicator"
        "location"
        "example"
        
    REQUIRES 
        core
        drivers
        common_utils
        mqtt
        esp_timer
        nvs_flash
        esp_netif
        esp_wifi
        efuse
        json
)