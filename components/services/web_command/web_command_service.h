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