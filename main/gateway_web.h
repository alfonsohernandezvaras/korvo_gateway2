#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t gateway_web_register(httpd_handle_t server);
#ifdef __cplusplus
}
#endif
