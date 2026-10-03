#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ap_running;
    char ssid[33];
    char bssid[18];
    uint8_t channel;
    uint16_t frequency_mhz;
    uint16_t clients;

    /* Enrolled radio peer. These fields are populated only when a currently
     * associated WiFi station matches expected_peer.mac from enrollment. */
    bool peer_visible;
    char peer_mac[18];
    int8_t peer_rssi_dbm;
    char signal_band[12];
    char enrolled_mac[18];
    bool peer_mac_match;

    /* Candidate is diagnostic/enrollment-only. It never grants CONTROL OK. */
    bool candidate_visible;
    char candidate_mac[18];
    int8_t candidate_rssi_dbm;
    char candidate_signal_band[12];
} gateway_wifi_status_t;

void gateway_wifi_get_status(gateway_wifi_status_t *status);
const char *gateway_wifi_signal_band(int rssi_dbm);
size_t gateway_wifi_status_json(char *out, size_t out_len);

/* Diagnostic override. When enabled, Status/JSON report this synthetic link. */
void gateway_wifi_set_simulation(bool enabled, const char *ssid, const char *peer_mac,
                                 uint8_t channel, int8_t rssi_dbm, uint16_t clients);

#ifdef __cplusplus
}
#endif
