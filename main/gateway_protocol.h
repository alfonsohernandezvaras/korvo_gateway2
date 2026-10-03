#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "gateway_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GATEWAY_PROTOCOL_VERSION 1
#define GATEWAY_V2_LAYER_VERSION "2.5.1"

esp_err_t gateway_protocol_init(void);

/* Transport-independent command dispatcher. The WebSocket layer will pass
 * parsed command name + primitive parameters here. */
esp_err_t gateway_protocol_command(const char *name,
                                   uint32_t arg_u32_a,
                                   uint32_t arg_u32_b,
                                   uint32_t arg_u32_c,
                                   char *ack_json,
                                   size_t ack_json_len);

/* Build current state snapshot for sys.status / Status page. */
size_t gateway_protocol_status_json(char *out, size_t out_len);

/* Identity envelope used when KORVO first connects. Pair secret is never exposed. */
size_t gateway_protocol_hello_json(char *out, size_t out_len);

/* Convert an internal event into the frozen KORVO JSON envelope. */
size_t gateway_protocol_event_json(const gateway_event_t *event, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
