idf_component_register(
    SRCS
        "src/service_manager.c"
        "timer/timer_service.c"
        "indicator/indicator_service.c"
        "connectivity/connectivity_service.c"
        "connectivity/gsm/gsm_service.c"
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
        "connectivity/wifi"
        "connectivity/mqtt"
        "indicator"
        "location"
        "example"
        
    REQUIRES 
        core
        drivers
)