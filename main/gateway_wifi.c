#include "gateway_wifi.h"
#include "gateway_enrollment.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_wifi.h"

static bool s_sim_enabled;
static gateway_wifi_status_t s_sim;

static void mac_to_text(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char *gateway_wifi_signal_band(int rssi_dbm)
{
    if (rssi_dbm > -55) return "excellent";
    if (rssi_dbm >= -65) return "good";
    if (rssi_dbm >= -75) return "fair";
    if (rssi_dbm >= -85) return "weak";
    return "poor";
}

static uint16_t channel_to_mhz(uint8_t channel)
{
    if (channel >= 1 && channel <= 13) return (uint16_t)(2407 + channel * 5);
    if (channel == 14) return 2484;
    return 0;
}

void gateway_wifi_get_status(gateway_wifi_status_t *status)
{
    if (!status) return;
    if (s_sim_enabled) {
        *status = s_sim;
        return;
    }
    memset(status, 0, sizeof(*status));
    status->ap_running = true;

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK) {
        snprintf(status->ssid, sizeof(status->ssid), "%s", (const char *)cfg.ap.ssid);
    }

    uint8_t bssid[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_AP, bssid) == ESP_OK) mac_to_text(bssid, status->bssid);

    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    uint8_t ch = 0;
    if (esp_wifi_get_channel(&ch, &second) == ESP_OK) {
        status->channel = ch;
        status->frequency_mhz = channel_to_mhz(ch);
    }

    gateway_enrollment_status_t enroll;
    gateway_enrollment_get_status(&enroll);
    snprintf(status->enrolled_mac, sizeof(status->enrolled_mac), "%s", enroll.expected_peer.mac);

    wifi_sta_list_t list;
    memset(&list, 0, sizeof(list));
    if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return;
    status->clients = list.num;

    for (int i = 0; i < list.num; ++i) {
        char mac[18];
        mac_to_text(list.sta[i].mac, mac);

        /* First associated station is only a candidate for explicit ENROLL.
         * It is never considered the operational peer unless its MAC matches
         * expected_peer.mac from NVS. */
        if (!status->candidate_visible) {
            status->candidate_visible = true;
            snprintf(status->candidate_mac, sizeof(status->candidate_mac), "%s", mac);
            status->candidate_rssi_dbm = list.sta[i].rssi;
            snprintf(status->candidate_signal_band, sizeof(status->candidate_signal_band), "%s",
                     gateway_wifi_signal_band(list.sta[i].rssi));
        }

        if (enroll.expected_peer.mac[0] && strcasecmp(mac, enroll.expected_peer.mac) == 0) {
            status->peer_visible = true;
            status->peer_mac_match = true;
            snprintf(status->peer_mac, sizeof(status->peer_mac), "%s", mac);
            status->peer_rssi_dbm = list.sta[i].rssi;
            snprintf(status->signal_band, sizeof(status->signal_band), "%s",
                     gateway_wifi_signal_band(list.sta[i].rssi));
            break;
        }
    }
}

size_t gateway_wifi_status_json(char *out, size_t out_len)
{
    if (!out || !out_len) return 0;
    gateway_wifi_status_t s;
    gateway_wifi_get_status(&s);
    int n = snprintf(out, out_len,
        "{\"ap_running\":%s,\"ssid\":\"%s\",\"bssid\":\"%s\",\"channel\":%u,\"frequency_mhz\":%u,"
        "\"clients\":%u,\"peer_visible\":%s,\"peer_mac\":\"%s\",\"rssi_dbm\":%d,\"signal_band\":\"%s\","
        "\"enrolled_mac\":\"%s\",\"peer_mac_match\":%s,"
        "\"candidate_visible\":%s,\"candidate_mac\":\"%s\",\"candidate_rssi_dbm\":%d,\"candidate_signal_band\":\"%s\"}",
        s.ap_running ? "true" : "false", s.ssid, s.bssid, s.channel, s.frequency_mhz,
        s.clients, s.peer_visible ? "true" : "false", s.peer_mac, (int)s.peer_rssi_dbm, s.signal_band,
        s.enrolled_mac, s.peer_mac_match ? "true" : "false",
        s.candidate_visible ? "true" : "false", s.candidate_mac, (int)s.candidate_rssi_dbm, s.candidate_signal_band);
    if (n < 0) return 0;
    return (size_t)n < out_len ? (size_t)n : out_len - 1;
}

void gateway_wifi_set_simulation(bool enabled, const char *ssid, const char *peer_mac,
                                 uint8_t channel, int8_t rssi_dbm, uint16_t clients)
{
    s_sim_enabled = enabled;
    if (!enabled) {
        memset(&s_sim, 0, sizeof(s_sim));
        return;
    }
    memset(&s_sim, 0, sizeof(s_sim));
    s_sim.ap_running = true;
    snprintf(s_sim.ssid, sizeof(s_sim.ssid), "%s", ssid ? ssid : "SIM-WIFI");
    snprintf(s_sim.bssid, sizeof(s_sim.bssid), "02:00:00:00:00:01");
    s_sim.channel = channel;
    s_sim.frequency_mhz = channel_to_mhz(channel);
    s_sim.clients = clients;

    gateway_enrollment_status_t e;
    gateway_enrollment_get_status(&e);
    snprintf(s_sim.enrolled_mac, sizeof(s_sim.enrolled_mac), "%s", e.expected_peer.mac);

    if (peer_mac && *peer_mac) {
        s_sim.candidate_visible = true;
        snprintf(s_sim.candidate_mac, sizeof(s_sim.candidate_mac), "%s", peer_mac);
        s_sim.candidate_rssi_dbm = rssi_dbm;
        snprintf(s_sim.candidate_signal_band, sizeof(s_sim.candidate_signal_band), "%s", gateway_wifi_signal_band(rssi_dbm));
        if (e.expected_peer.mac[0] && strcasecmp(peer_mac, e.expected_peer.mac) == 0) {
            s_sim.peer_visible = true;
            s_sim.peer_mac_match = true;
            snprintf(s_sim.peer_mac, sizeof(s_sim.peer_mac), "%s", peer_mac);
            s_sim.peer_rssi_dbm = rssi_dbm;
            snprintf(s_sim.signal_band, sizeof(s_sim.signal_band), "%s", gateway_wifi_signal_band(rssi_dbm));
        }
    }
}
