/**
 * @file components/services/web_command/web_command_service.h
 * @brief Web Command Service – header file.
 * =============================================================================
 * This service subscribes to MQTT topics for web commands and processes them.
 * It parses incoming JSON commands and executes corresponding system commands via
 * the command router. It also handles device registration and configuration updates.
 * =============================================================================
 * 
 * @version 1.0.0
 * @author matthithyahu
 * @date 2026-04-01
 */


#ifndef WEB_COMMAND_SERVICE_H
#define WEB_COMMAND_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_command_service_init(void);
esp_err_t web_command_service_register_handlers(void);
esp_err_t web_command_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_COMMAND_SERVICE_H */