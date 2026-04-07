idf_component_register(
    SRCS
        "src/service_manager.c"
        "timer/timer_service.c"
        
        # "indicator/indicator_service.c"

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

        "sensing/pir/pir_service.c"
        "sensing/ultrasonic/ultrasonic_service.c"

        "actuation/led/led_service.c"
        "actuation/buzzer/buzzer_service.c"
        "actuation/servo/servo_service.c"

        "example/example_service.c"


    INCLUDE_DIRS 
        "include"
        "auth"
        "fault"
        "power"
        "timer"
        "config"

        "sensing"
        "sensing/pir"
        "sensing/ultrasonic"

        "actuation"
        "actuation/led"
        "actuation/buzzer"
        "actuation/servo"

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