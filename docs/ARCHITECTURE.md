
an_intelligent_iot_based_smart_bin_system
|
|
|   .clangd
|   .gitignore
|   CMakeLists.txt
|   dependencies.lock
|   partitions.csv
|   sdkconfig
|   sdkconfig.old
|
+---.vscode
|       c_cpp_properties.json
|       launch.json
|       settings.json
|
+---components
|   +---config
|   |   |   CMakeLists.txt
|   |   |   Kconfig.projbuild
|   |   |   {Kconfig.projbuild}
|   |   |
|   |   \---include
|   |           system_config.h
|   |
|   +---core
|   |   |   CMakeLists.txt
|   |   |   
|   |   +---include
|   |   |       command_router.h
|   |   |       command_types.h
|   |   |       core_types.h
|   |   |       event_dispatcher.h
|   |   |       event_types.h
|   |   |       state_manager.h
|   |   |       system_state.h
|   |   |
|   |   +---priv_include
|   |   |       core_internal.h
|   |   |
|   |   \---src
|   |           command_router.c
|   |           event_dispatcher.c
|   |           state_manager.c
|   |
|   +---drivers
|   |   |   CMakeLists.txt
|   |   |
|   |   +---actuators
|   |   |   +---buzzer
|   |   |   |       buzzer.c
|   |   |   |       buzzer.h
|   |   |   |       
|   |   |   +---lcd
|   |   |   |       example_lcd_i2c_main.c
|   |   |   |       lcd_configs.h
|   |   |   |       lcd_i2c.c
|   |   |   |       lcd_i2c.h
|   |   |   |       lcd_i2c_priv.h
|   |   |   |
|   |   |   +---led
|   |   |   |       led.c
|   |   |   |       led.h
|   |   |   |
|   |   |   \---servo
|   |   |           servo_driver.c
|   |   |           servo_driver.h
|   |   |
|   |   +---comms
|   |   \---sensors
|   |       +---gps
|   |       +---pir
|   |       |       pir_driver.c
|   |       |       pir_driver.h
|   |       |
|   |       +---radar
|   |       \---ultrasonic
|   |               ultrasonic_driver.c
|   |               ultrasonic_driver.h
|   |
|   +---services
|   |   |   CMakeLists.txt
|   |   |
|   |   +---actuation
|   |   |       actuation_service.c
|   |   |       actuation_service.h
|   |   |
|   |   +---connectivity
|   |   |   |   connectivity_service.c
|   |   |   |   connectivity_service.h
|   |   |   |
|   |   |   +---gsm
|   |   |   +---include
|   |   |   +---mqtt
|   |   |   \---wifi
|   |   +---include
|   |   |       service_interfaces.h
|   |   |
|   |   +---locating
|   |   +---sensing
|   |   |       sensing_service.c
|   |   |       sensing_service.h
|   |   |
|   |   +---storage
|   |   |       storage_service.c
|   |   |       storage_service.h
|   |   |
|   |   \---ui
|   |           ui_service.c
|   |           ui_service.h
|   |
|   \---utils
|       |   CMakeLists.txt
|       |
|       +---include
|       |       utils_common.h
|       |
|       \---src
|               utils_common.c
|
+---docs
|   |   ARCHITECTURE.md
|   |   during_designing_an_intelligent_iot_based_smart_dustbin_system.txt
|   |   HINTS.md
|   |   README.md
|   |   TODO.md
|   |   
|   \---adr
+---main
|       CMakeLists.txt
|       main.c
|
\---tests
    |   CMakeLists.txt
    |
    +---include
    |       test_harness.h
    |
    \---src
            test_harness.c


    