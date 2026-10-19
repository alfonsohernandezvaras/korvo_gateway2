#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
void gateway_rtp_monitor_start(void);
void gateway_rtp_monitor_active(bool active);
esp_err_t gateway_rtp_monitor_register(httpd_handle_t server);

void gateway_rtp_monitor_remote(const char *ip, uint16_t port);
