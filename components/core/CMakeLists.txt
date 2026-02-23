idf_component_register(
    SRCS 
        "src/command_router.c"
        "src/event_dispatcher.c"
        "src/state_manager.c"
        "src/system_state.c"
        "src/core_self_test.c"
    INCLUDE_DIRS 
        "include"
    PRIV_INCLUDE_DIRS
        # "priv_include"
    REQUIRES 
        esp_timer
        esp_event
        services
)