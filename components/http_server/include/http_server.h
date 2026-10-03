/* HTTP server public API.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

httpd_handle_t start_webserver(uint16_t port);

/* Shared authentication helpers for Gateway V2 pages registered by main/. */
bool http_server_password_enabled(void);
bool http_server_request_authenticated(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
