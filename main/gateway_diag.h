#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "gateway_services.h"
#include "gateway_enrollment.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Diagnostic simulator: every externally visible condition can be reproduced
 * without field hardware. Simulation emits the same event names as real I/O,
 * with simulated=true in the internal event envelope. */
void gateway_diag_init(void);
void gateway_diag_sim_wiegand(uint8_t format, uint32_t facility, uint32_t card, const char *raw_hex);
void gateway_diag_sim_wiegand_reader(uint8_t reader, uint8_t format, uint32_t facility, uint32_t card, const char *raw_hex);
void gateway_diag_sim_rex(bool active);
void gateway_diag_sim_door(bool open);
void gateway_diag_sim_door_forced(void);
void gateway_diag_sim_door_held_open(void);
void gateway_diag_sim_call_short(uint32_t held_ms);
void gateway_diag_sim_call_long(uint32_t held_ms);
void gateway_diag_sim_sip(gateway_sip_state_t state, const char *direction,
                          const char *extension, const char *name, const char *call_id);
void gateway_diag_sim_sip_registered(bool registered);
void gateway_diag_sim_bt(bool connected, const char *device, const char *profile);
void gateway_diag_sim_audio(bool ready);
void gateway_diag_sim_fault(const char *fault_name, const char *detail);
void gateway_diag_sim_enrollment(const gateway_identity_t *peer, bool pair_auth_ok);
void gateway_diag_sim_peer_offline(void);
void gateway_diag_sim_wifi(bool enabled, const char *ssid, const char *peer_mac,
                           uint8_t channel, int8_t rssi_dbm, uint16_t clients);

/* Physical output tests from Diagnostic page */
esp_err_t gateway_diag_test_relay(uint32_t pulse_ms);
esp_err_t gateway_diag_test_beacon(bool on);
esp_err_t gateway_diag_test_beacon_flash(uint32_t on_ms, uint32_t off_ms, uint32_t cycles);
esp_err_t gateway_diag_test_wiegand_tx(uint8_t format, uint32_t facility, uint32_t card);

#ifdef __cplusplus
}
#endif
