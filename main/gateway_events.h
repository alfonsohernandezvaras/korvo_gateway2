#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GATEWAY_EVENT_NAME_MAX 40
#define GATEWAY_EVENT_DATA_MAX 320
#define GATEWAY_EVENT_LISTENERS_MAX 6

typedef struct {
    uint32_t seq;
    int64_t timestamp_us;
    bool simulated;
    char name[GATEWAY_EVENT_NAME_MAX];
    char data[GATEWAY_EVENT_DATA_MAX];
} gateway_event_t;

typedef void (*gateway_event_listener_t)(const gateway_event_t *event, void *ctx);

esp_err_t gateway_events_init(void);
esp_err_t gateway_events_add_listener(gateway_event_listener_t listener, void *ctx);
void gateway_event_emit(const char *name, const char *data_json, bool simulated);
uint32_t gateway_event_last_seq(void);

#ifdef __cplusplus
}
#endif
