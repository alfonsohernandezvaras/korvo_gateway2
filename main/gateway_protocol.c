#include "gateway_protocol.h"
#include "gateway_io.h"
#include "gateway_map.h"
#include "gateway_services.h"
#include "gateway_enrollment.h"
#include "gateway_wifi.h"
#include "korvo_protocol.h"

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_timer.h"

/* GW_V6_1_DICTIONARY_IDENTITY */
/* FNV-1a diagnostic fingerprint of [id, direction, name, NUL].
 * NOT cryptographic. KORVO comparison is a future stage.
 */
static uint32_t gateway_dictionary_fingerprint(void)
{
    uint32_t hash = UINT32_C(2166136261);
    for (unsigned id = 0; id <= 255; ++id) {
        if (!korvo_protocol_is_valid((uint8_t)id)) continue;
        const char *name = korvo_protocol_name((korvo_message_id_t)id);
        hash = (hash ^ (uint8_t)id) * UINT32_C(16777619);
        hash = (hash ^ (uint8_t)korvo_protocol_direction((korvo_message_id_t)id)) * UINT32_C(16777619);
        for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
            hash = (hash ^ *p) * UINT32_C(16777619);
        hash = (hash ^ 0u) * UINT32_C(16777619);
    }
    return hash;
}
static unsigned gateway_dictionary_count(void)
{
    unsigned n = 0;
    for (unsigned id = 0; id <= 255; ++id)
        if (korvo_protocol_is_valid((uint8_t)id)) ++n;
    return n;
}

esp_err_t gateway_protocol_init(void)
{
    return ESP_OK;
}

static void ack(char *out, size_t len, const char *name, bool ok, const char *error)
{
    if (!out || !len) return;
    snprintf(out, len, "{\"v\":1,\"type\":\"ack\",\"name\":\"%s\",\"ok\":%s%s%s%s}",
             name ? name : "", ok ? "true" : "false",
             error ? ",\"error\":\"" : "", error ? error : "", error ? "\"" : "");
}

static bool command_requires_enrolled_control(const char *name)
{
    if (!name) return false;
    return !strncmp(name, "door.", 5) ||
           !strncmp(name, "beacon.", 7) ||
           !strncmp(name, "wiegand.", 8) ||
           !strncmp(name, "sip.call", 8) ||
           !strncmp(name, "sip.answer", 10) ||
           !strncmp(name, "sip.reject", 10) ||
           !strncmp(name, "sip.hangup", 10);
}

esp_err_t gateway_protocol_command(const char *name,
                                   uint32_t a, uint32_t b, uint32_t c,
                                   char *ack_json, size_t ack_json_len)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;

    /* Status remains readable on a mismatched pair so KORVO can show exactly
     * what was detected. Anything capable of changing field state is denied. */
    if (!strcmp(name, "sys.ping") || !strcmp(name, "sys.status") || !strcmp(name, "sip.status")) {
        ack(ack_json, ack_json_len, name, true, NULL);
        return ESP_OK;
    }

    if (command_requires_enrolled_control(name) && !gateway_enrollment_control_allowed()) {
        ack(ack_json, ack_json_len, name, false, "peer_not_enrolled");
        return ESP_ERR_INVALID_STATE;
    }

    if (!strcmp(name, "door.unlock")) err = gateway_door_unlock(a ? a : 5000);
    else if (!strcmp(name, "door.lock")) err = gateway_door_lock();
    else if (!strcmp(name, "beacon.on")) err = gateway_beacon_set(true);
    else if (!strcmp(name, "beacon.off")) err = gateway_beacon_set(false);
    else if (!strcmp(name, "beacon.flash")) err = gateway_beacon_flash(a, b, c);
    else if (!strcmp(name, "wiegand.tx")) err = gateway_wiegand_tx((uint8_t)a, b, c);
    else if (!strcmp(name, "sip.call")) err = gateway_sip_request(GW_SIP_ACTION_CALL);
    else if (!strcmp(name, "sip.answer")) err = gateway_sip_request(GW_SIP_ACTION_ANSWER);
    else if (!strcmp(name, "sip.reject")) err = gateway_sip_request(GW_SIP_ACTION_REJECT);
    else if (!strcmp(name, "sip.hangup")) err = gateway_sip_request(GW_SIP_ACTION_HANGUP);
    else {
        ack(ack_json, ack_json_len, name, false, "unknown_command");
        return ESP_ERR_NOT_FOUND;
    }

    ack(ack_json, ack_json_len, name, err == ESP_OK, err == ESP_OK ? NULL : "command_failed");
    return err;
}

size_t gateway_protocol_hello_json(char *out, size_t out_len)
{
    if (!out || !out_len) return 0;
    gateway_enrollment_status_t e;
    gateway_enrollment_get_status(&e);
    int n = snprintf(out, out_len,
        "{\"v\":1,\"type\":\"event\",\"name\":\"sys.hello\",\"data\":{"
        "\"role\":\"gateway\",\"layer_version\":\"%s\","
        "\"device_name\":\"%s\",\"hostname\":\"%s\",\"location\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\","
        "\"pair_mode\":\"ip\",\"korvo_ip\":\"%s\",\"enrollment\":\"%s\",\"control_ok\":%s,\"dictionary_version\":%u,\"dictionary_count\":%u,\"dictionary_fingerprint\":\"%08lX\"}}",
        GATEWAY_V2_LAYER_VERSION,
        e.local.device_name, e.local.hostname, e.local.location, e.local.ip, e.local.mac,
        e.expected_peer.ip, gateway_enrollment_state_name(e.state), e.control_allowed ? "true" : "false",
        (unsigned)KORVO_PROTOCOL_DICTIONARY_VERSION, gateway_dictionary_count(),
        (unsigned long)gateway_dictionary_fingerprint());
    if (n < 0) return 0;
    return (size_t)n < out_len ? (size_t)n : out_len - 1;
}

size_t gateway_protocol_status_json(char *out, size_t out_len)
{
    if (!out || !out_len) return 0;
    gateway_io_status_t io;
    gateway_sip_status_t sip;
    gateway_bt_status_t bt;
    gateway_sip_config_t sip_cfg;
    gateway_io_get_status(&io);
    gateway_sip_get_status(&sip);
    gateway_sip_get_config(&sip_cfg);
    /* G2-03B: estado diagnostico, NO indica registro SIP. */
    bool sip_config_ready = sip_cfg.server[0] && sip_cfg.extension[0] &&
                            sip_cfg.password[0] && sip_cfg.central_extension[0] &&
                            sip_cfg.port > 0;
    gateway_bt_get_status(&bt);

    char map_json[160];
    gateway_map_status_json(map_json, sizeof(map_json));
    char enroll_json[1400];
    char wifi_json[700];
    gateway_enrollment_status_json(enroll_json, sizeof(enroll_json));
    gateway_wifi_status_json(wifi_json, sizeof(wifi_json));

    uint64_t uptime_s = (uint64_t)(esp_timer_get_time() / 1000000LL);
    uint32_t heap = esp_get_free_heap_size();

    int n = snprintf(out, out_len,
        "{\"v\":1,\"type\":\"status\",\"name\":\"sys.status\",\"data\":{"
        "\"layer_version\":\"%s\",\"dictionary_version\":%u,\"dictionary_count\":%u,\"dictionary_fingerprint\":\"%08lX\",\"uptime_s\":%llu,\"free_heap\":%lu,"
        "\"io\":{\"rex\":%s,\"door\":\"%s\",\"relay\":%s,\"beacon\":%s},"
        "\"wiegand\":{\"last_bits\":%lu,\"last_raw\":\"%llX\"},"
        "\"sip\":{\"engine\":\"stub\",\"config_ready\":%s,\"registered\":%s,\"state\":\"%s\",\"direction\":\"%s\",\"remote_extension\":\"%s\",\"remote_name\":\"%s\",\"extension\":\"%s\",\"extension_name\":\"%s\",\"central_extension\":\"%s\",\"central_name\":\"%s\",\"call_mode\":\"%s\"},"
        "\"bluetooth\":{\"engine\":\"stub\",\"connected\":%s,\"audio_ready\":%s,\"device\":\"%s\",\"profile\":\"%s\"},"
        "\"wifi\":%s,\"enrollment\":%s,\"gpio_map\":%s}}",
        GATEWAY_V2_LAYER_VERSION,
        (unsigned)KORVO_PROTOCOL_DICTIONARY_VERSION, gateway_dictionary_count(),
        (unsigned long)gateway_dictionary_fingerprint(),
        (unsigned long long)uptime_s, (unsigned long)heap,
        io.rex_active ? "true" : "false", io.door_open ? "open" : "closed",
        io.relay_on ? "true" : "false", io.beacon_on ? "true" : "false",
        (unsigned long)io.last_wiegand_bits, (unsigned long long)io.last_wiegand_raw,
        sip_config_ready ? "true" : "false", sip.registered ? "true" : "false", gateway_sip_state_name(sip.state), sip.direction,
        sip.remote_extension, sip.remote_name, sip_cfg.extension, sip_cfg.extension_name,
        sip_cfg.central_extension, sip_cfg.central_name, gateway_call_mode_name(sip_cfg.call_mode),
        bt.connected ? "true" : "false", bt.audio_ready ? "true" : "false", bt.device, bt.profile,
        wifi_json, enroll_json, map_json);
    if (n < 0) return 0;
    return (size_t)n < out_len ? (size_t)n : out_len - 1;
}

size_t gateway_protocol_event_json(const gateway_event_t *event, char *out, size_t out_len)
{
    if (!event || !out || !out_len) return 0;
    int n = snprintf(out, out_len,
        "{\"v\":1,\"seq\":%lu,\"type\":\"event\",\"name\":\"%s\",\"simulated\":%s,\"data\":%s}",
        (unsigned long)event->seq, event->name, event->simulated ? "true" : "false",
        event->data[0] ? event->data : "{}");
    if (n < 0) return 0;
    return (size_t)n < out_len ? (size_t)n : out_len - 1;
}
