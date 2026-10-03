#include "gateway_events.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "gw_events";

typedef struct {
    gateway_event_listener_t fn;
    void *ctx;
} listener_slot_t;

static listener_slot_t s_listeners[GATEWAY_EVENT_LISTENERS_MAX];
static SemaphoreHandle_t s_lock;
static uint32_t s_seq;

esp_err_t gateway_events_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t gateway_events_add_listener(gateway_event_listener_t listener, void *ctx)
{
    if (!listener) return ESP_ERR_INVALID_ARG;
    esp_err_t init_err = gateway_events_init();
    if (init_err != ESP_OK) return init_err;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    for (size_t i = 0; i < GATEWAY_EVENT_LISTENERS_MAX; ++i) {
        if (!s_listeners[i].fn) {
            s_listeners[i].fn = listener;
            s_listeners[i].ctx = ctx;
            xSemaphoreGive(s_lock);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_lock);
    return ESP_ERR_NO_MEM;
}

uint32_t gateway_event_last_seq(void)
{
    return s_seq;
}

void gateway_event_emit(const char *name, const char *data_json, bool simulated)
{
    if (!name || !*name) return;
    if (!s_lock && gateway_events_init() != ESP_OK) return;

    gateway_event_t ev = {0};
    ev.timestamp_us = esp_timer_get_time();
    ev.simulated = simulated;
    snprintf(ev.name, sizeof(ev.name), "%s", name);
    snprintf(ev.data, sizeof(ev.data), "%s", data_json ? data_json : "{}");

    listener_slot_t snapshot[GATEWAY_EVENT_LISTENERS_MAX] = {0};
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        ev.seq = ++s_seq;
        memcpy(snapshot, s_listeners, sizeof(snapshot));
        xSemaphoreGive(s_lock);
    }

    ESP_LOGI(TAG, "#%lu %s%s %s", (unsigned long)ev.seq, ev.name,
             simulated ? " [SIM]" : "", ev.data);

    for (size_t i = 0; i < GATEWAY_EVENT_LISTENERS_MAX; ++i) {
        if (snapshot[i].fn) snapshot[i].fn(&ev, snapshot[i].ctx);
    }
}
