#include "gateway_enrollment.h"
#include "gateway_events.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs.h"

static const char *TAG = "gw_enroll";
static const char *NVS_NS = "gwv2";

static gateway_enrollment_status_t s_status;
static gateway_enrollment_config_t s_cfg;

static void copy_text(char *dst, size_t len, const char *src)
{
    if (!dst || !len) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < len && src[i]) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

static bool text_set(const char *s) { return s && s[0]; }
static bool text_match(const char *expected, const char *observed)
{
    return text_set(expected) && text_set(observed) && strcasecmp(expected, observed) == 0;
}

static void mac_to_text(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* V2.5: the only persistent pairing authority is the KORVO IPv4 address. */
static bool config_complete(void)
{
    return text_set(s_cfg.expected_peer.ip);
}

static void persist_string(nvs_handle_t h, const char *key, const char *value)
{
    esp_err_t e = nvs_set_str(h, key, value ? value : "");
    if (e != ESP_OK) ESP_LOGW(TAG, "NVS set %s failed: %s", key, esp_err_to_name(e));
}

static void load_string(nvs_handle_t h, const char *key, char *dst, size_t len)
{
    size_t needed = len;
    if (nvs_get_str(h, key, dst, &needed) != ESP_OK) dst[0] = '\0';
    dst[len - 1] = '\0';
}

static void persist_config(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    persist_string(h, "dev_name", s_status.local.device_name);
    persist_string(h, "location", s_status.local.location);
    persist_string(h, "peer_name", s_cfg.expected_peer.device_name);
    persist_string(h, "peer_host", s_cfg.expected_peer.hostname);
    persist_string(h, "peer_loc", s_cfg.expected_peer.location);
    persist_string(h, "peer_ip", s_cfg.expected_peer.ip);
    persist_string(h, "peer_mac", s_cfg.expected_peer.mac);
    /* Clear V2.4 credentials during the first V2.5 save. */
    persist_string(h, "pair_id", "");
    persist_string(h, "pair_key", "");
    nvs_commit(h);
    nvs_close(h);
}

static void load_config(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    load_string(h, "dev_name", s_status.local.device_name, sizeof(s_status.local.device_name));
    load_string(h, "location", s_status.local.location, sizeof(s_status.local.location));
    load_string(h, "peer_name", s_cfg.expected_peer.device_name, sizeof(s_cfg.expected_peer.device_name));
    load_string(h, "peer_host", s_cfg.expected_peer.hostname, sizeof(s_cfg.expected_peer.hostname));
    load_string(h, "peer_loc", s_cfg.expected_peer.location, sizeof(s_cfg.expected_peer.location));
    load_string(h, "peer_ip", s_cfg.expected_peer.ip, sizeof(s_cfg.expected_peer.ip));
    load_string(h, "peer_mac", s_cfg.expected_peer.mac, sizeof(s_cfg.expected_peer.mac));
    /* V2.4 Pair ID/Key are deliberately ignored in V2.5. */
    s_cfg.pair_id[0] = '\0';
    s_cfg.pair_key[0] = '\0';
    nvs_close(h);
}

static void recompute(bool emit)
{
    gateway_enrollment_state_t old_state = s_status.state;
    bool old_control = s_status.control_allowed;

    s_status.expected_peer = s_cfg.expected_peer;
    s_status.pair_id[0] = '\0';

    if (!config_complete()) {
        s_status.state = GW_ENROLL_UNCONFIGURED;
        s_status.ip_match = false;
        s_status.hostname_match = s_status.name_match = false;
        s_status.location_match = s_status.mac_match = false;
        s_status.gateway_ok = false;
        s_status.control_allowed = false;
    } else if (!s_status.peer_connected) {
        s_status.state = GW_ENROLL_OFFLINE;
        s_status.ip_match = false;
        s_status.hostname_match = s_status.name_match = false;
        s_status.location_match = s_status.mac_match = false;
        s_status.gateway_ok = false;
        s_status.control_allowed = false;
    } else {
        s_status.ip_match = text_match(s_cfg.expected_peer.ip, s_status.observed_peer.ip);
        /* Metadata matches are informational only in V2.5. Missing metadata is
         * not a mismatch and MAC never authorizes GPIO/control. */
        s_status.hostname_match = !text_set(s_cfg.expected_peer.hostname) || text_match(s_cfg.expected_peer.hostname, s_status.observed_peer.hostname);
        s_status.name_match = !text_set(s_cfg.expected_peer.device_name) || text_match(s_cfg.expected_peer.device_name, s_status.observed_peer.device_name);
        s_status.location_match = !text_set(s_cfg.expected_peer.location) || text_match(s_cfg.expected_peer.location, s_status.observed_peer.location);
        s_status.mac_match = !text_set(s_cfg.expected_peer.mac) || text_match(s_cfg.expected_peer.mac, s_status.observed_peer.mac);

        /* V2.5 authoritative pair = reciprocal configured IP plus a live
         * WebSocket whose TCP source is exactly the configured KORVO IP. */
        s_status.gateway_ok = s_status.ip_match && s_status.pair_auth_ok;
        s_status.control_allowed = s_status.gateway_ok;
        if (!s_status.ip_match) s_status.state = GW_ENROLL_MISMATCH;
        else s_status.state = s_status.pair_auth_ok ? GW_ENROLL_OK : GW_ENROLL_AUTHENTICATING;
    }

    if (emit && (old_state != s_status.state || old_control != s_status.control_allowed)) {
        char payload[240];
        snprintf(payload, sizeof(payload),
                 "{\"state\":\"%s\",\"gateway_ok\":%s,\"control_ok\":%s,\"peer_ip\":\"%s\"}",
                 gateway_enrollment_state_name(s_status.state),
                 s_status.gateway_ok ? "true" : "false",
                 s_status.control_allowed ? "true" : "false",
                 s_status.observed_peer.ip);
        gateway_event_emit("enrollment.status", payload, false);
        gateway_event_emit(s_status.control_allowed ? "control.ok" : "control.blocked", payload, false);
    }
}

esp_err_t gateway_enrollment_init(const char *network_hostname)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_status.state = GW_ENROLL_UNCONFIGURED;
    copy_text(s_status.local.hostname, sizeof(s_status.local.hostname), network_hostname);
    load_config();
    if (!s_status.local.device_name[0]) copy_text(s_status.local.device_name, sizeof(s_status.local.device_name), network_hostname);

    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK) mac_to_text(mac, s_status.local.mac);
    else if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK) mac_to_text(mac, s_status.local.mac);

    recompute(false);
    ESP_LOGI(TAG, "Gateway identity: name='%s' host='%s' location='%s' MAC=%s; KORVO IP='%s'",
             s_status.local.device_name, s_status.local.hostname, s_status.local.location,
             s_status.local.mac, s_cfg.expected_peer.ip);
    return ESP_OK;
}

esp_err_t gateway_enrollment_set_local_tags(const char *device_name, const char *location)
{
    if (!device_name || !location) return ESP_ERR_INVALID_ARG;
    copy_text(s_status.local.device_name, sizeof(s_status.local.device_name), device_name);
    copy_text(s_status.local.location, sizeof(s_status.local.location), location);
    persist_config();
    recompute(true);
    return ESP_OK;
}

void gateway_enrollment_set_local_ip(const char *ip)
{
    copy_text(s_status.local.ip, sizeof(s_status.local.ip), ip);
}

esp_err_t gateway_enrollment_set_expected_peer(const gateway_enrollment_config_t *config)
{
    if (!config || !config->expected_peer.ip[0]) return ESP_ERR_INVALID_ARG;
    bool changed = strcasecmp(s_cfg.expected_peer.ip, config->expected_peer.ip) != 0;
    s_cfg = *config;
    s_cfg.pair_id[0] = '\0';
    s_cfg.pair_key[0] = '\0';
    if (changed) {
        memset(&s_status.observed_peer, 0, sizeof(s_status.observed_peer));
        s_status.peer_connected = false;
        s_status.pair_auth_ok = false;
    }
    persist_config();
    recompute(true);
    return ESP_OK;
}

esp_err_t gateway_enrollment_clear_expected_peer(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(&s_status.expected_peer, 0, sizeof(s_status.expected_peer));
    memset(&s_status.observed_peer, 0, sizeof(s_status.observed_peer));
    s_status.peer_connected = false;
    s_status.pair_auth_ok = false;
    persist_config();
    recompute(true);
    return ESP_OK;
}

void gateway_enrollment_peer_seen(const gateway_identity_t *peer, bool pair_auth_ok)
{
    if (!peer) return;
    s_status.observed_peer = *peer;
    s_status.peer_connected = true;
    s_status.pair_auth_ok = pair_auth_ok;
    recompute(true);
}

void gateway_enrollment_peer_offline(void)
{
    s_status.peer_connected = false;
    s_status.pair_auth_ok = false;
    recompute(true);
}

void gateway_enrollment_get_status(gateway_enrollment_status_t *status)
{
    if (status) *status = s_status;
}

bool gateway_enrollment_control_allowed(void)
{
    return s_status.control_allowed;
}

bool gateway_enrollment_has_expected_peer(void)
{
    return config_complete();
}

bool gateway_enrollment_verify_pair_hmac(const char *nonce, const char *pair_id, const char *hmac_hex)
{
    (void)nonce; (void)pair_id; (void)hmac_hex;
    return false;
}

const char *gateway_enrollment_state_name(gateway_enrollment_state_t state)
{
    switch (state) {
        case GW_ENROLL_UNCONFIGURED: return "unconfigured";
        case GW_ENROLL_OFFLINE: return "offline";
        case GW_ENROLL_AUTHENTICATING: return "connecting";
        case GW_ENROLL_OK: return "paired";
        case GW_ENROLL_MISMATCH: return "mismatch";
        case GW_ENROLL_AUTH_FAILED: return "blocked";
        default: return "unknown";
    }
}

size_t gateway_enrollment_status_json(char *out, size_t out_len)
{
    if (!out || !out_len) return 0;
    int n = snprintf(out, out_len,
        "{\"state\":\"%s\",\"configured\":%s,\"paired\":%s,\"gateway_ok\":%s,\"control_ok\":%s,"
        "\"session_ip_ok\":%s,"
        "\"local\":{\"name\":\"%s\",\"hostname\":\"%s\",\"location\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\"},"
        "\"expected_peer\":{\"name\":\"%s\",\"hostname\":\"%s\",\"location\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\"},"
        "\"observed_peer\":{\"name\":\"%s\",\"hostname\":\"%s\",\"location\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\"},"
        "\"match\":{\"ip\":%s,\"name\":%s,\"hostname\":%s,\"location\":%s,\"mac\":%s}}",
        gateway_enrollment_state_name(s_status.state), config_complete() ? "true" : "false",
        s_status.gateway_ok ? "true" : "false",
        s_status.gateway_ok ? "true" : "false", s_status.control_allowed ? "true" : "false",
        s_status.pair_auth_ok ? "true" : "false",
        s_status.local.device_name, s_status.local.hostname, s_status.local.location, s_status.local.ip, s_status.local.mac,
        s_status.expected_peer.device_name, s_status.expected_peer.hostname, s_status.expected_peer.location, s_status.expected_peer.ip, s_status.expected_peer.mac,
        s_status.observed_peer.device_name, s_status.observed_peer.hostname, s_status.observed_peer.location, s_status.observed_peer.ip, s_status.observed_peer.mac,
        s_status.ip_match ? "true" : "false", s_status.name_match ? "true" : "false",
        s_status.hostname_match ? "true" : "false", s_status.location_match ? "true" : "false",
        s_status.mac_match ? "true" : "false");
    if (n < 0) return 0;
    return (size_t)n < out_len ? (size_t)n : out_len - 1;
}
