/**
 * @file wifi_driver_abstraction.c
 * @brief Implementation of the WiFi driver abstraction.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses ESP‑IDF's esp_wifi, esp_event, and esp_netif components.
 * - Internal context is static; all functions operate on that single instance.
 * - A mutex protects the context from concurrent access.
 * - Driver event handlers forward events to the registered user callback.
 *
 * =============================================================================
 * @version 1.0.0
 * @date 2026-02-24
 * @author System Architecture Team
 * =============================================================================
 */

#include "wifi_driver_abstraction.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define WIFI_MAX_SSID_LEN   32
#define WIFI_MAX_PASS_LEN   64

/* Private context (static) */
typedef struct {
    bool initialized;                       /**< true after wifi_driver_init() succeeds */
    bool started;                            /**< true after wifi_driver_start() succeeds */
    wifi_driver_event_cb_t user_cb;          /**< User‑registered callback function */
    esp_netif_t *sta_netif;                  /**< Handle to the station network interface */
    SemaphoreHandle_t mutex;                  /**< Mutex for thread‑safe context access */
} wifi_driver_context_t;

static wifi_driver_context_t s_ctx = {0};

/* Lock helpers */
static void lock_context(void) {
    if (s_ctx.mutex) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    }
}

static void unlock_context(void) {
    if (s_ctx.mutex) {
        xSemaphoreGive(s_ctx.mutex);
    }
}

/* Event handlers (called from ESP‑IDF event task) */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    lock_context();
    wifi_driver_event_cb_t cb = s_ctx.user_cb;
    unlock_context();

    if (!cb) {
        return;
    }

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_CONNECTED:
                cb(WIFI_DRIVER_EVENT_CONNECTED, NULL);
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* disc = (wifi_event_sta_disconnected_t*)event_data;
                cb(WIFI_DRIVER_EVENT_DISCONNECTED, &disc->reason);
                break;
            }
            default:
                break;
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    lock_context();
    wifi_driver_event_cb_t cb = s_ctx.user_cb;
    unlock_context();

    if (cb) {
        ip_event_got_ip_t* got_ip = (ip_event_got_ip_t*)event_data;
        cb(WIFI_DRIVER_EVENT_GOT_IP, &got_ip->ip_info);
    }
}

/* Public API ----------------------------------------------------------------*/

esp_err_t wifi_driver_init(void)
{
    esp_err_t ret;

    lock_context();
    if (s_ctx.initialized) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }

    /* Create mutex if not already done (should be NULL now) */
    if (!s_ctx.mutex) {
        s_ctx.mutex = xSemaphoreCreateMutex();
        if (!s_ctx.mutex) {
            unlock_context();
            return ESP_ERR_NO_MEM;
        }
    }
    unlock_context();

    /* Create default station netif */
    s_ctx.sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_ctx.sta_netif) {
        /* Keep mutex for possible future re‑init */
        return ESP_FAIL;
    }

    /* Initialize WiFi stack */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Register event handlers */
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    lock_context();
    s_ctx.initialized = true;
    unlock_context();

    return ESP_OK;
}

esp_err_t wifi_driver_start(void)
{
    esp_err_t ret;

    lock_context();
    if (!s_ctx.initialized) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctx.started) {
        unlock_context();
        return ESP_OK;   /* Already started, idempotent */
    }
    unlock_context();

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        return ret;
    }

    lock_context();
    s_ctx.started = true;
    unlock_context();

    return ESP_OK;
}

esp_err_t wifi_driver_connect(const char* ssid, const char* password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t ssid_len = strnlen(ssid, WIFI_MAX_SSID_LEN);
    size_t pass_len = strnlen(password, WIFI_MAX_PASS_LEN);
    if (ssid_len == 0 || ssid_len > WIFI_MAX_SSID_LEN - 1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pass_len > WIFI_MAX_PASS_LEN - 1) {
        return ESP_ERR_INVALID_ARG;
    }

    lock_context();
    if (!s_ctx.initialized || !s_ctx.started) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    unlock_context();

    wifi_config_t wifi_cfg = {0};
    strlcpy((char*)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    if (pass_len > 0) {
        strlcpy((char*)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));
    }
    /* Leave threshold etc. at defaults (0) */

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_connect();
    return ret;
}

esp_err_t wifi_driver_disconnect(void)
{
    lock_context();
    if (!s_ctx.initialized || !s_ctx.started) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    unlock_context();

    return esp_wifi_disconnect();
}

esp_err_t wifi_driver_stop(void)
{
    lock_context();
    if (!s_ctx.initialized) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_ctx.started) {
        unlock_context();
        return ESP_OK;   /* Already stopped */
    }
    unlock_context();

    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_OK) {
        lock_context();
        s_ctx.started = false;
        unlock_context();
    }
    return ret;
}

esp_err_t wifi_driver_get_ip(esp_netif_ip_info_t* ip_info)
{
    if (!ip_info) {
        return ESP_ERR_INVALID_ARG;
    }

    lock_context();
    if (!s_ctx.initialized || !s_ctx.sta_netif) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    unlock_context();

    return esp_netif_get_ip_info(s_ctx.sta_netif, ip_info);
}

esp_err_t wifi_driver_register_callback(wifi_driver_event_cb_t cb)
{
    lock_context();
    s_ctx.user_cb = cb;
    unlock_context();
    return ESP_OK;
}