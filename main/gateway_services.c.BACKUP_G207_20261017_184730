#include "gateway_services.h"
#include "gateway_events.h"
extern void gateway_sip_register_start(void);
extern void gateway_sip_call_start(void);
extern void gateway_sip_incoming_start(void);

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "gw_services";
static gateway_sip_status_t s_sip;
static gateway_bt_status_t s_bt;
static gateway_sip_config_t s_cfg;
static gateway_sip_action_handler_t s_action_handler;
static void *s_action_ctx;

static const char *NVS_NS = "gwv2";

static void load_str(nvs_handle_t h, const char *key, char *dst, size_t len)
{
    size_t n = len;
    if (nvs_get_str(h, key, dst, &n) != ESP_OK) dst[0] = '\0';
    if (len) dst[len - 1] = '\0';
}

static void load_config_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    load_str(h, "sip_srv", s_cfg.server, sizeof(s_cfg.server));
    load_str(h, "sip_ext", s_cfg.extension, sizeof(s_cfg.extension));
    load_str(h, "sip_name", s_cfg.extension_name, sizeof(s_cfg.extension_name));
    load_str(h, "sip_pass", s_cfg.password, sizeof(s_cfg.password));
    load_str(h, "sip_c_ext", s_cfg.central_extension, sizeof(s_cfg.central_extension));
    load_str(h, "sip_c_name", s_cfg.central_name, sizeof(s_cfg.central_name));
    uint16_t port = 0;
    uint32_t timeout = 0;
    uint8_t mode = 0, beacon = 0;
    if (nvs_get_u16(h, "sip_port", &port) == ESP_OK && port) s_cfg.port = port;
    if (nvs_get_u32(h, "sip_timeout", &timeout) == ESP_OK && timeout) s_cfg.call_timeout_s = timeout;
    if (nvs_get_u8(h, "sip_mode", &mode) == ESP_OK && mode <= GW_CALL_DISABLED) s_cfg.call_mode = (gateway_call_mode_t)mode;
    if (nvs_get_u8(h, "sip_beacon", &beacon) == ESP_OK) s_cfg.beacon_on_incoming = beacon != 0;
    nvs_close(h);
}

static void persist_config_to_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "sip_srv", s_cfg.server);
    nvs_set_u16(h, "sip_port", s_cfg.port);
    nvs_set_str(h, "sip_ext", s_cfg.extension);
    nvs_set_str(h, "sip_name", s_cfg.extension_name);
    nvs_set_str(h, "sip_pass", s_cfg.password);
    nvs_set_str(h, "sip_c_ext", s_cfg.central_extension);
    nvs_set_str(h, "sip_c_name", s_cfg.central_name);
    nvs_set_u8(h, "sip_mode", (uint8_t)s_cfg.call_mode);
    nvs_set_u32(h, "sip_timeout", s_cfg.call_timeout_s);
    nvs_set_u8(h, "sip_beacon", s_cfg.beacon_on_incoming ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

static bool call_mode_can_send(void)
{
    return s_cfg.call_mode == GW_CALL_SEND_RECEIVE || s_cfg.call_mode == GW_CALL_SEND_ONLY;
}

static bool call_mode_can_receive(void)
{
    return s_cfg.call_mode == GW_CALL_SEND_RECEIVE || s_cfg.call_mode == GW_CALL_RECEIVE_ONLY;
}

static void call_button_listener(const gateway_event_t *event, void *ctx)
{
    (void)ctx;
    if (!event) return;

    if (!strcmp(event->name, "call.short_press")) {
        if (s_cfg.call_mode == GW_CALL_DISABLED) return;
        if (s_sip.state == GW_SIP_INCOMING && call_mode_can_receive()) {
            (void)gateway_sip_request(GW_SIP_ACTION_ANSWER);
        } else if (s_sip.state == GW_SIP_CALLING || s_sip.state == GW_SIP_RINGING ||
                   s_sip.state == GW_SIP_CONNECTED) {
            (void)gateway_sip_request(GW_SIP_ACTION_HANGUP);
        } else if ((s_sip.state == GW_SIP_IDLE || s_sip.state == GW_SIP_ENDED ||
                    s_sip.state == GW_SIP_FAILED || s_sip.state == GW_SIP_BUSY ||
                    s_sip.state == GW_SIP_NO_ANSWER || s_sip.state == GW_SIP_REJECTED) && call_mode_can_send()) {
            ESP_LOGI(TAG, "INVITE DIAG: outgoing CALL disabled for this test");
        }
    } else if (!strcmp(event->name, "call.long_press")) {
        if (s_sip.state == GW_SIP_INCOMING && call_mode_can_receive()) {
            (void)gateway_sip_request(GW_SIP_ACTION_REJECT);
        } else if (s_sip.state == GW_SIP_CALLING || s_sip.state == GW_SIP_RINGING ||
                   s_sip.state == GW_SIP_CONNECTED) {
            (void)gateway_sip_request(GW_SIP_ACTION_HANGUP);
        }
    }
}

void gateway_services_init(void)
{
    memset(&s_sip, 0, sizeof(s_sip));
    memset(&s_bt, 0, sizeof(s_bt));
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_sip.state = GW_SIP_IDLE;
    s_cfg.port = 5060;
    s_cfg.call_mode = GW_CALL_SEND_RECEIVE;
    s_cfg.call_timeout_s = 30;
    s_cfg.beacon_on_incoming = true;
    load_config_from_nvs();
    gateway_sip_register_start();
    gateway_sip_incoming_start();
    gateway_sip_call_start(); /* G2-04 R4: controlled SIP diagnostic re-enable */
    (void)gateway_events_add_listener(call_button_listener, NULL);
}

const char *gateway_sip_state_name(gateway_sip_state_t state)
{
    switch (state) {
        case GW_SIP_IDLE: return "idle";
        case GW_SIP_CALLING: return "calling";
        case GW_SIP_RINGING: return "ringing";
        case GW_SIP_INCOMING: return "incoming";
        case GW_SIP_CONNECTED: return "connected";
        case GW_SIP_BUSY: return "busy";
        case GW_SIP_NO_ANSWER: return "no_answer";
        case GW_SIP_REJECTED: return "rejected";
        case GW_SIP_FAILED: return "failed";
        case GW_SIP_ENDED: return "ended";
        default: return "unknown";
    }
}

const char *gateway_call_mode_name(gateway_call_mode_t mode)
{
    switch (mode) {
        case GW_CALL_SEND_RECEIVE: return "send_receive";
        case GW_CALL_SEND_ONLY: return "send_only";
        case GW_CALL_RECEIVE_ONLY: return "receive_only";
        case GW_CALL_DISABLED: return "disabled";
        default: return "unknown";
    }
}

void gateway_sip_set_config(const gateway_sip_config_t *config)
{
    if (!config) return;
    s_cfg = *config;
    if (!s_cfg.port) s_cfg.port = 5060;
    if (!s_cfg.call_timeout_s) s_cfg.call_timeout_s = 30;
    persist_config_to_nvs();
}

void gateway_sip_get_config(gateway_sip_config_t *config)
{
    if (config) *config = s_cfg;
}

void gateway_sip_register_action_handler(gateway_sip_action_handler_t handler, void *ctx)
{
    s_action_handler = handler;
    s_action_ctx = ctx;
}

esp_err_t gateway_sip_request(gateway_sip_action_t action)
{
    if (!s_action_handler) {
        ESP_LOGW(TAG, "SIP action %d requested but SIP engine is not attached", (int)action);
        gateway_event_emit("sip.engine_unavailable", "{}", false);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return s_action_handler(action, s_action_ctx);
}

void gateway_sip_set_registered(bool registered, bool simulated)
{
    if (s_sip.registered == registered) return;
    s_sip.registered = registered;
    gateway_event_emit(registered ? "sip.registered" : "sip.unregistered", "{}", simulated);
}

void gateway_sip_set_state(gateway_sip_state_t state, const char *direction,
                           const char *extension, const char *name,
                           const char *call_id, bool simulated)
{
    s_sip.state = state;
    snprintf(s_sip.direction, sizeof(s_sip.direction), "%s", direction ? direction : "");
    snprintf(s_sip.remote_extension, sizeof(s_sip.remote_extension), "%s", extension ? extension : "");
    snprintf(s_sip.remote_name, sizeof(s_sip.remote_name), "%s", name ? name : "");
    snprintf(s_sip.call_id, sizeof(s_sip.call_id), "%s", call_id ? call_id : "");

    char payload[240];
    snprintf(payload, sizeof(payload),
             "{\"direction\":\"%s\",\"remote_extension\":\"%s\",\"remote_name\":\"%s\",\"call_id\":\"%s\"}",
             s_sip.direction, s_sip.remote_extension, s_sip.remote_name, s_sip.call_id);
    char event[40];
    snprintf(event, sizeof(event), "sip.%s", gateway_sip_state_name(state));
    gateway_event_emit(event, payload, simulated);
}

void gateway_sip_get_status(gateway_sip_status_t *status)
{
    if (status) *status = s_sip;
}

void gateway_bt_set_state(bool connected, const char *device, const char *profile, bool simulated)
{
    s_bt.connected = connected;
    snprintf(s_bt.device, sizeof(s_bt.device), "%s", device ? device : "");
    snprintf(s_bt.profile, sizeof(s_bt.profile), "%s", profile ? profile : "");
    char payload[160];
    snprintf(payload, sizeof(payload), "{\"device\":\"%s\",\"profile\":\"%s\"}", s_bt.device, s_bt.profile);
    gateway_event_emit(connected ? "bt.connected" : "bt.disconnected", payload, simulated);
}

void gateway_audio_set_ready(bool ready, bool simulated)
{
    s_bt.audio_ready = ready;
    gateway_event_emit(ready ? "audio.ready" : "audio.not_ready", "{}", simulated);
}

void gateway_bt_get_status(gateway_bt_status_t *status)
{
    if (status) *status = s_bt;
}
