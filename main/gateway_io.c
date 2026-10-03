#include "gateway_io.h"
#include "gateway_events.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "gateway_io";
static gateway_io_config_t s_cfg;
static gateway_io_status_t s_status;
static TimerHandle_t s_relay_timer;
static portMUX_TYPE s_wiegand_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint64_t s_wiegand_raw;
static volatile uint8_t s_wiegand_bits;
static volatile int64_t s_wiegand_last_us;
static bool s_isr_service_owned;

static inline int output_level(bool active, bool active_high)
{
    return (active == active_high) ? 1 : 0;
}

static void emit_simple_state(const char *name, bool simulated)
{
    gateway_event_emit(name, "{}", simulated);
}

static void set_relay_internal(bool on, bool emit)
{
    gpio_set_level(GW_GPIO_DOOR_RELAY, output_level(on, s_cfg.relay_active_high));
    bool changed = (s_status.relay_on != on);
    s_status.relay_on = on;
    if (emit && changed) emit_simple_state(on ? "door.relay_on" : "door.relay_off", false);
}

static void relay_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    set_relay_internal(false, true);
}

esp_err_t gateway_door_unlock(uint32_t ms)
{
    if (ms == 0) ms = 5000;
    set_relay_internal(true, true);
    if (s_relay_timer) {
        xTimerStop(s_relay_timer, 0);
        xTimerChangePeriod(s_relay_timer, pdMS_TO_TICKS(ms), 0);
        xTimerStart(s_relay_timer, 0);
    }
    return ESP_OK;
}

esp_err_t gateway_door_lock(void)
{
    if (s_relay_timer) xTimerStop(s_relay_timer, 0);
    set_relay_internal(false, true);
    return ESP_OK;
}

esp_err_t gateway_beacon_set(bool on)
{
    gpio_set_level(GW_GPIO_BEACON, output_level(on, s_cfg.beacon_active_high));
    bool changed = (s_status.beacon_on != on);
    s_status.beacon_on = on;
    if (changed) gateway_event_emit(on ? "beacon.on" : "beacon.off", "{}", false);
    return ESP_OK;
}

typedef struct {
    uint32_t on_ms;
    uint32_t off_ms;
    uint32_t cycles;
} beacon_flash_args_t;

static void beacon_flash_task(void *arg)
{
    beacon_flash_args_t cfg = *(beacon_flash_args_t *)arg;
    free(arg);
    for (uint32_t i = 0; i < cfg.cycles; ++i) {
        gateway_beacon_set(true);
        vTaskDelay(pdMS_TO_TICKS(cfg.on_ms));
        gateway_beacon_set(false);
        if (i + 1 < cfg.cycles) vTaskDelay(pdMS_TO_TICKS(cfg.off_ms));
    }
    vTaskDelete(NULL);
}

esp_err_t gateway_beacon_flash(uint32_t on_ms, uint32_t off_ms, uint32_t cycles)
{
    if (!on_ms) on_ms = 500;
    if (!off_ms) off_ms = 500;
    if (!cycles) cycles = 3;
    if (cycles > 100) cycles = 100;

    beacon_flash_args_t *args = malloc(sizeof(*args));
    if (!args) return ESP_ERR_NO_MEM;
    *args = (beacon_flash_args_t){ .on_ms = on_ms, .off_ms = off_ms, .cycles = cycles };
    if (xTaskCreate(beacon_flash_task, "gw_beacon", 2048, args, 5, NULL) != pdPASS) {
        free(args);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void IRAM_ATTR wiegand_isr(void *arg)
{
    uint32_t bit = (uint32_t)(uintptr_t)arg;
    portENTER_CRITICAL_ISR(&s_wiegand_mux);
    if (s_wiegand_bits < 64) {
        s_wiegand_raw = (s_wiegand_raw << 1) | (bit & 1U);
        s_wiegand_bits++;
        s_wiegand_last_us = esp_timer_get_time();
    }
    portEXIT_CRITICAL_ISR(&s_wiegand_mux);
}

static int parity_even_u64(uint64_t v, uint8_t bits)
{
    int ones = 0;
    for (uint8_t i = 0; i < bits; ++i) ones += (v >> i) & 1U;
    return (ones & 1) == 0;
}

static void process_wiegand_frame(uint64_t raw, uint8_t bits)
{
    uint32_t facility = 0;
    uint32_t card = 0;
    const char *parity = "unchecked";

    if (bits == 26) {
        uint32_t data = (uint32_t)((raw >> 1) & 0xFFFFFFU);
        facility = (data >> 16) & 0xFFU;
        card = data & 0xFFFFU;
        uint32_t first12 = (data >> 12) & 0xFFFU;
        uint32_t last12 = data & 0xFFFU;
        int p_even = (raw >> 25) & 1U;
        int p_odd = raw & 1U;
        bool ok = (parity_even_u64(((uint64_t)p_even << 12) | first12, 13) &&
                   !parity_even_u64(((uint64_t)last12 << 1) | p_odd, 13));
        parity = ok ? "ok" : "bad";
    } else if (bits == 34) {
        uint32_t data = (uint32_t)((raw >> 1) & 0xFFFFFFFFULL);
        facility = (data >> 16) & 0xFFFFU;
        card = data & 0xFFFFU;
    }

    s_status.last_wiegand_bits = bits;
    s_status.last_wiegand_raw = raw;

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"reader\":1,\"format\":%u,\"facility\":%" PRIu32 ",\"card\":%" PRIu32 ",\"raw\":\"%0*llX\",\"parity\":\"%s\",\"source\":\"gpio\"}",
             bits, facility, card, (bits + 3) / 4, (unsigned long long)raw, parity);
    gateway_event_emit("reader.card", payload, false);
}

static bool input_active(int gpio, bool active_low)
{
    int level = gpio_get_level(gpio);
    return active_low ? (level == 0) : (level != 0);
}

static void gateway_input_task(void *arg)
{
    (void)arg;
    bool rex_last = input_active(GW_GPIO_REX, s_cfg.rex_active_low);
    bool door_last = (gpio_get_level(GW_GPIO_DOOR_SENSOR) != 0) == s_cfg.door_open_high;
    bool call_last = input_active(GW_GPIO_CALL_BUTTON, s_cfg.call_active_low);
    int64_t call_pressed_us = call_last ? esp_timer_get_time() : 0;
    bool long_sent = false;

    s_status.rex_active = rex_last;
    s_status.door_open = door_last;
    s_status.call_pressed = call_last;

    while (true) {
        int64_t now = esp_timer_get_time();
        uint64_t wg_raw = 0;
        uint8_t wg_bits = 0;
        int64_t wg_last = 0;

        portENTER_CRITICAL(&s_wiegand_mux);
        if (s_wiegand_bits > 0 && (now - s_wiegand_last_us) >= (int64_t)s_cfg.wiegand_frame_gap_ms * 1000LL) {
            wg_raw = s_wiegand_raw;
            wg_bits = s_wiegand_bits;
            wg_last = s_wiegand_last_us;
            s_wiegand_raw = 0;
            s_wiegand_bits = 0;
            s_wiegand_last_us = 0;
        }
        portEXIT_CRITICAL(&s_wiegand_mux);
        (void)wg_last;
        if (wg_bits) process_wiegand_frame(wg_raw, wg_bits);

        bool rex = input_active(GW_GPIO_REX, s_cfg.rex_active_low);
        if (rex != rex_last) {
            rex_last = rex;
            s_status.rex_active = rex;
            emit_simple_state(rex ? "rex.active" : "rex.inactive", false);
        }

        bool door_open = (gpio_get_level(GW_GPIO_DOOR_SENSOR) != 0) == s_cfg.door_open_high;
        if (door_open != door_last) {
            door_last = door_open;
            s_status.door_open = door_open;
            emit_simple_state(door_open ? "door.open" : "door.closed", false);
        }

        bool call = input_active(GW_GPIO_CALL_BUTTON, s_cfg.call_active_low);
        if (call != call_last) {
            call_last = call;
            s_status.call_pressed = call;
            if (call) {
                call_pressed_us = now;
                long_sent = false;
            } else {
                int64_t held_ms = call_pressed_us ? (now - call_pressed_us) / 1000LL : 0;
                if (!long_sent) {
                    char payload[96];
                    snprintf(payload, sizeof(payload), "{\"held_ms\":%lld}", (long long)held_ms);
                    gateway_event_emit("call.short_press", payload, false);
                }
                call_pressed_us = 0;
                long_sent = false;
            }
        }
        if (call && !long_sent && call_pressed_us &&
            (now - call_pressed_us) >= (int64_t)s_cfg.call_long_press_ms * 1000LL) {
            char payload[96];
            snprintf(payload, sizeof(payload), "{\"held_ms\":%lu}", (unsigned long)s_cfg.call_long_press_ms);
            gateway_event_emit("call.long_press", payload, false);
            long_sent = true;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

gateway_io_config_t gateway_io_default_config(void)
{
    return (gateway_io_config_t){
        .rex_active_low = true,
        .door_open_high = true,
        .call_active_low = true,
        .relay_active_high = true,
        .beacon_active_high = true,
        .call_long_press_ms = 2000,
        .wiegand_frame_gap_ms = 30,
    };
}

esp_err_t gateway_io_init(const gateway_io_config_t *config)
{
    s_cfg = config ? *config : gateway_io_default_config();
    memset(&s_status, 0, sizeof(s_status));

    gpio_config_t wg_out = {
        .pin_bit_mask = (1ULL << GW_GPIO_WIEGAND_D0_OUT) |
                        (1ULL << GW_GPIO_WIEGAND_D1_OUT),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&wg_out), TAG, "Wiegand output gpio_config");
    /* Open-drain: 1 releases the line, 0 creates the Wiegand pulse.
     * External pull-up / field interface is required. */
    gpio_set_level(GW_GPIO_WIEGAND_D0_OUT, 1);
    gpio_set_level(GW_GPIO_WIEGAND_D1_OUT, 1);

    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << GW_GPIO_DOOR_RELAY) | (1ULL << GW_GPIO_BEACON),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "relay/beacon gpio_config");
    gpio_set_level(GW_GPIO_DOOR_RELAY, output_level(false, s_cfg.relay_active_high));
    gpio_set_level(GW_GPIO_BEACON, output_level(false, s_cfg.beacon_active_high));

    gpio_config_t wg_in = {
        .pin_bit_mask = (1ULL << GW_GPIO_WIEGAND_D0_IN) | (1ULL << GW_GPIO_WIEGAND_D1_IN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, /* GPIO35/36 have no internal pullups */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&wg_in), TAG, "Wiegand gpio_config");

    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << GW_GPIO_REX) | (1ULL << GW_GPIO_DOOR_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "REX/door gpio_config");

    gpio_config_t call_input = {
        .pin_bit_mask = (1ULL << GW_GPIO_CALL_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    /* GPIO39 has no internal pullup; external conditioning/pull resistor is required. */
    ESP_RETURN_ON_ERROR(gpio_config(&call_input), TAG, "CALL gpio_config");

    esp_err_t isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (isr_err == ESP_OK) s_isr_service_owned = true;
    else if (isr_err != ESP_ERR_INVALID_STATE) return isr_err;
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(GW_GPIO_WIEGAND_D0_IN, wiegand_isr, (void *)(uintptr_t)0), TAG, "D0 ISR");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(GW_GPIO_WIEGAND_D1_IN, wiegand_isr, (void *)(uintptr_t)1), TAG, "D1 ISR");

    s_relay_timer = xTimerCreate("gw_relay", pdMS_TO_TICKS(5000), pdFALSE, NULL, relay_timer_cb);
    if (!s_relay_timer) return ESP_ERR_NO_MEM;

    if (xTaskCreate(gateway_input_task, "gw_inputs", 4096, NULL, 8, NULL) != pdPASS) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "GPIO V1 initialized: D0in=%d D1in=%d D0out=%d D1out=%d REX=%d DOOR=%d CALL=%d RELAY=%d BEACON=%d",
             GW_GPIO_WIEGAND_D0_IN, GW_GPIO_WIEGAND_D1_IN, GW_GPIO_WIEGAND_D0_OUT,
             GW_GPIO_WIEGAND_D1_OUT, GW_GPIO_REX, GW_GPIO_DOOR_SENSOR,
             GW_GPIO_CALL_BUTTON, GW_GPIO_DOOR_RELAY, GW_GPIO_BEACON);
    return ESP_OK;
}

void gateway_io_get_status(gateway_io_status_t *status)
{
    if (status) *status = s_status;
}

static uint64_t make_w26(uint32_t facility, uint32_t card)
{
    uint32_t data = ((facility & 0xFFU) << 16) | (card & 0xFFFFU);
    uint32_t first12 = (data >> 12) & 0xFFFU;
    uint32_t last12 = data & 0xFFFU;
    uint64_t p_even = parity_even_u64(first12, 12) ? 0 : 1;
    uint64_t p_odd = parity_even_u64(last12, 12) ? 1 : 0;
    return (p_even << 25) | ((uint64_t)data << 1) | p_odd;
}

static uint64_t make_w34(uint32_t facility, uint32_t card)
{
    uint32_t data = ((facility & 0xFFFFU) << 16) | (card & 0xFFFFU);
    uint32_t first16 = (data >> 16) & 0xFFFFU;
    uint32_t last16 = data & 0xFFFFU;
    uint64_t p_even = parity_even_u64(first16, 16) ? 0 : 1;
    uint64_t p_odd = parity_even_u64(last16, 16) ? 1 : 0;
    return (p_even << 33) | ((uint64_t)data << 1) | p_odd;
}

esp_err_t gateway_wiegand_tx(uint8_t format, uint32_t facility, uint32_t card)
{
    uint64_t frame;
    if (format == 26) frame = make_w26(facility, card);
    else if (format == 34) frame = make_w34(facility, card);
    else return ESP_ERR_NOT_SUPPORTED;

    for (int bit = format - 1; bit >= 0; --bit) {
        int gpio = ((frame >> bit) & 1ULL) ? GW_GPIO_WIEGAND_D1_OUT : GW_GPIO_WIEGAND_D0_OUT;
        gpio_set_level(gpio, 0);
        esp_rom_delay_us(50);
        gpio_set_level(gpio, 1);
        esp_rom_delay_us(1950);
    }

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"format\":%u,\"facility\":%" PRIu32 ",\"card\":%" PRIu32 "}",
             format, facility, card);
    gateway_event_emit("wiegand.tx_done", payload, false);
    return ESP_OK;
}
