#include "gateway_diag.h"
#include "gateway_events.h"
#include "gateway_io.h"
#include "gateway_enrollment.h"
#include "gateway_wifi.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void gateway_diag_init(void) {}

void gateway_diag_sim_wiegand_reader(uint8_t reader, uint8_t format, uint32_t facility, uint32_t card, const char *raw_hex)
{
    if (reader != 1 && reader != 2) reader = 1;
    char payload[240];
    snprintf(payload, sizeof(payload),
             "{\"reader\":%u,\"format\":%u,\"facility\":%" PRIu32 ",\"card\":%" PRIu32 ",\"raw\":\"%s\",\"source\":\"simulator\"}",
             reader, format, facility, card, raw_hex ? raw_hex : "");
    gateway_event_emit("reader.card", payload, true);
}

void gateway_diag_sim_wiegand(uint8_t format, uint32_t facility, uint32_t card, const char *raw_hex)
{
    gateway_diag_sim_wiegand_reader(1, format, facility, card, raw_hex);
}

void gateway_diag_sim_rex(bool active)
{
    gateway_event_emit(active ? "rex.active" : "rex.inactive", "{}", true);
}

void gateway_diag_sim_door(bool open)
{
    gateway_event_emit(open ? "door.open" : "door.closed", "{}", true);
}

void gateway_diag_sim_door_forced(void)
{
    gateway_event_emit("door.forced", "{}", true);
}

void gateway_diag_sim_door_held_open(void)
{
    gateway_event_emit("door.held_open", "{}", true);
}

void gateway_diag_sim_call_short(uint32_t held_ms)
{
    char payload[80];
    snprintf(payload, sizeof(payload), "{\"held_ms\":%" PRIu32 "}", held_ms);
    gateway_event_emit("call.short_press", payload, true);
}

void gateway_diag_sim_call_long(uint32_t held_ms)
{
    char payload[80];
    snprintf(payload, sizeof(payload), "{\"held_ms\":%" PRIu32 "}", held_ms);
    gateway_event_emit("call.long_press", payload, true);
}

void gateway_diag_sim_sip(gateway_sip_state_t state, const char *direction,
                          const char *extension, const char *name, const char *call_id)
{
    gateway_sip_set_state(state, direction, extension, name, call_id, true);
}

void gateway_diag_sim_sip_registered(bool registered)
{
    gateway_sip_set_registered(registered, true);
}

void gateway_diag_sim_bt(bool connected, const char *device, const char *profile)
{
    gateway_bt_set_state(connected, device, profile, true);
}

void gateway_diag_sim_audio(bool ready)
{
    gateway_audio_set_ready(ready, true);
}

void gateway_diag_sim_fault(const char *fault_name, const char *detail)
{
    char name[64];
    snprintf(name, sizeof(name), "fault.%s", (fault_name && *fault_name) ? fault_name : "generic");
    char payload[200];
    snprintf(payload, sizeof(payload), "{\"detail\":\"%s\"}", detail ? detail : "");
    gateway_event_emit(name, payload, true);
}

void gateway_diag_sim_enrollment(const gateway_identity_t *peer, bool pair_auth_ok)
{
    gateway_enrollment_peer_seen(peer, pair_auth_ok);
}

void gateway_diag_sim_peer_offline(void)
{
    gateway_enrollment_peer_offline();
}

void gateway_diag_sim_wifi(bool enabled, const char *ssid, const char *peer_mac,
                           uint8_t channel, int8_t rssi_dbm, uint16_t clients)
{
    gateway_wifi_set_simulation(enabled, ssid, peer_mac, channel, rssi_dbm, clients);
    char payload[180];
    snprintf(payload, sizeof(payload),
             "{\"enabled\":%s,\"ssid\":\"%s\",\"peer_mac\":\"%s\",\"channel\":%u,\"rssi_dbm\":%d}",
             enabled ? "true" : "false", ssid ? ssid : "", peer_mac ? peer_mac : "", channel, rssi_dbm);
    gateway_event_emit("wifi.simulated", payload, true);
}

esp_err_t gateway_diag_test_relay(uint32_t pulse_ms)
{
    return gateway_door_unlock(pulse_ms ? pulse_ms : 5000);
}

esp_err_t gateway_diag_test_beacon(bool on)
{
    return gateway_beacon_set(on);
}

esp_err_t gateway_diag_test_beacon_flash(uint32_t on_ms, uint32_t off_ms, uint32_t cycles)
{
    return gateway_beacon_flash(on_ms, off_ms, cycles);
}

esp_err_t gateway_diag_test_wiegand_tx(uint8_t format, uint32_t facility, uint32_t card)
{
    return gateway_wiegand_tx(format, facility, card);
}
