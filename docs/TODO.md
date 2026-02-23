components/
│
├── core/                          ← CENTRAL BRAIN (COMPLETE)
│   ├── include/
│   │   ├── core_types.h           ← ✅ COMPLETE
│   │   ├── event_types.h          ← ✅ COMPLETE
│   │   ├── command_types.h        ← ✅ COMPLETE
│   │   ├── system_state.h         ← ✅ COMPLETE
│   │   ├── state_manager.h        ← ✅ COMPLETE
│   │   ├── command_router.h       ← ✅ COMPLETE
│   │   └── event_dispatcher.h     ← ✅ COMPLETE
│   │
│   ├── priv_include/
│   │   └── core_internal.h        ← ✅ COMPLETE
│   │
│   └── src/
│       ├── state_manager.c        ← ✅ COMPLETE (UPDATED)
│       ├── event_dispatcher.c     ← ✅ COMPLETE
│       └── command_router.c       ← ✅ COMPLETE
│
├── services/                      ← LOGIC PROVIDERS (INTERFACES READY)
│   ├── include/
│   │   └── service_interfaces.h   ← ✅ COMPLETE
│   │
│   ├── sensing/                   ← TO IMPLEMENT
│   │   ├── include/
│   │   │   └── sensing_service.h
│   │   └── src/
│   │       └── sensing_service.c
│   │
│   ├── actuation/                 ← TO IMPLEMENT
│   │   ├── include/
│   │   │   └── actuation_service.h
│   │   └── src/
│   │       └── actuation_service.c
│   │
│   ├── connectivity/              ← TO IMPLEMENT
│   │   ├── include/
│   │   │   └── connectivity_service.h
│   │   └── src/
│   │       └── connectivity_service.c
│   │
│   ├── storage/                   ← TO IMPLEMENT
│   │   ├── include/
│   │   │   └── storage_service.h
│   │   └── src/
│   │       └── storage_service.c
│   │
│   └── ui/                        ← TO IMPLEMENT
│       ├── include/
│       │   └── ui_service.h
│       └── src/
│           └── ui_service.c
│
├── drivers/                       ← HARDWARE ABSTRACTION (TO IMPLEMENT)
│   ├── pir/
│   │   ├── include/
│   │   │   └── pir_driver.h
│   │   └── src/
│   │       └── pir_driver.c
│   │
│   ├── ultrasonic/
│   │   ├── include/
│   │   │   └── ultrasonic_driver.h
│   │   └── src/
│   │       └── ultrasonic_driver.c
│   │
│   ├── servo/
│   │   ├── include/
│   │   │   └── servo_driver.h
│   │   └── src/
│   │       └── servo_driver.c
│   │
│   ├── lcd/
│   │   ├── include/
│   │   │   └── lcd_driver.h
│   │   └── src/
│   │       └── lcd_driver.c
│   │
│   └── keypad/
│       ├── include/
│       │   └── keypad_driver.h
│       └── src/
│           └── keypad_driver.c
│
├── config/                        ← SYSTEM CONFIGURATION (READY)
│   ├── include/
│   │   └── system_config.h        ← ✅ COMPLETE
│   │
│   └── Kconfig.projbuild          ← TO CREATE
│
└── utils/                         ← SHARED UTILITIES (READY)
    ├── include/
    │   └── utils_common.h         ← ✅ COMPLETE
    │
    └── src/
        └── utils_common.c         ← TO IMPLEMENT