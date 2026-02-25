idf_component_register(
    SRCS
        "src/service_manager.c"
        "timer/timer_service.c"
        "indicator/indicator_service.c"

        "connectivity/connectivity_service.c"
        "connectivity/gsm/gsm_service.c"
        "connectivity/wifi/src/wifi_driver_abstraction.c"
        "connectivity/wifi/src/wifi_service.c"
        "connectivity/mqtt/src/mqtt_client_abstraction.c"
        "connectivity/mqtt/src/mqtt_service.c"
        "connectivity/mqtt/src/mqtt_topic.c"

        "networking/bin_network_service/bin_network_service.c"

        "sensing/ultrasonic/ultrasonic_service.c"
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
        "sensing/ultrasonic"
        "actuation"
        "actuation/servo_service"

        "connectivity"
        "connectivity/gsm"
        "connectivity/wifi/include"
        "connectivity/mqtt/include"

        "networking/bin_network_service"

        "indicator"
        "location"
        "example"
        
    REQUIRES 
        core
        drivers
        mqtt
        esp_timer
        nvs_flash
        esp_netif
        esp_wifi
        efuse
        json
)