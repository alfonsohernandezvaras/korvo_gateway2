#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the single authenticated KORVO control WebSocket.
 * Operational peer IP/MAC are always taken from enrollment NVS. */
esp_err_t gateway_ws_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
