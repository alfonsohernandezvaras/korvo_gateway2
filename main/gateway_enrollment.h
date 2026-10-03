#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GW_ENROLL_TEXT_SMALL   32
#define GW_ENROLL_TEXT_MEDIUM  64
#define GW_ENROLL_PAIR_KEY_MAX 96

typedef enum {
    GW_ENROLL_UNCONFIGURED = 0,
    GW_ENROLL_OFFLINE,
    GW_ENROLL_AUTHENTICATING,
    GW_ENROLL_OK,
    GW_ENROLL_MISMATCH,
    GW_ENROLL_AUTH_FAILED,
} gateway_enrollment_state_t;

typedef struct {
    char device_name[GW_ENROLL_TEXT_MEDIUM];
    char hostname[GW_ENROLL_TEXT_MEDIUM];
    char location[GW_ENROLL_TEXT_MEDIUM];
    char ip[GW_ENROLL_TEXT_SMALL];
    char mac[18];
} gateway_identity_t;

typedef struct {
    gateway_identity_t expected_peer;
    /* Legacy V2.4 fields kept only so old NVS contents can be loaded safely.
     * V2.5 IP pairing does not use Pair ID / Pair Key for control. */
    char pair_id[GW_ENROLL_TEXT_MEDIUM];
    char pair_key[GW_ENROLL_PAIR_KEY_MAX];
} gateway_enrollment_config_t;

typedef struct {
    gateway_identity_t local;
    gateway_identity_t expected_peer;
    gateway_identity_t observed_peer;
    char pair_id[GW_ENROLL_TEXT_MEDIUM];
    gateway_enrollment_state_t state;
    bool peer_connected;
    /* V2.5: true when the live WebSocket source IP equals expected_peer.ip.
     * Field name is retained for source compatibility with older diagnostics. */
    bool pair_auth_ok;
    bool ip_match;
    bool hostname_match;
    bool name_match;
    bool location_match;
    bool mac_match;
    /* GATEWAY OK = configured KORVO IP is the live/verified peer IP.
     * CONTROL OK = GATEWAY OK + live WebSocket admitted from that IP. */
    bool gateway_ok;
    bool control_allowed;
} gateway_enrollment_status_t;

esp_err_t gateway_enrollment_init(const char *network_hostname);
esp_err_t gateway_enrollment_set_local_tags(const char *device_name, const char *location);
void gateway_enrollment_set_local_ip(const char *ip);

/* V2.5 pairing authority is expected_peer.ip only. Other identity fields are
 * diagnostic metadata and never gate GPIO/control. */
esp_err_t gateway_enrollment_set_expected_peer(const gateway_enrollment_config_t *config);
esp_err_t gateway_enrollment_clear_expected_peer(void);
bool gateway_enrollment_has_expected_peer(void);

/* pair_auth_ok means the peer owns a live WebSocket session whose TCP source
 * address equals the configured KORVO IP. */
void gateway_enrollment_peer_seen(const gateway_identity_t *peer, bool pair_auth_ok);
void gateway_enrollment_peer_offline(void);

void gateway_enrollment_get_status(gateway_enrollment_status_t *status);
bool gateway_enrollment_control_allowed(void);
const char *gateway_enrollment_state_name(gateway_enrollment_state_t state);

/* Legacy V2.4 API retained for ABI/source compatibility. V2.5 does not call it. */
bool gateway_enrollment_verify_pair_hmac(const char *nonce,
                                         const char *pair_id,
                                         const char *hmac_hex);

size_t gateway_enrollment_status_json(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
