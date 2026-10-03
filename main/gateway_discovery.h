#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "gateway_enrollment.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GW_DISCOVERY_NOT_FOUND = 0,
    GW_DISCOVERY_LEGACY_REACHABLE,
    GW_DISCOVERY_IDENTITY_OK,
    GW_DISCOVERY_INVALID_IDENTITY,
} gateway_discovery_state_t;

typedef struct {
    gateway_discovery_state_t state;
    int http_status;
    gateway_identity_t identity;
    char role[16];
    unsigned protocol;
    char firmware[32];
    char message[96];
} gateway_discovery_result_t;

/* Probe the KORVO identity endpoint at one explicitly supplied IPv4 address.
 * V2.4 uses this only for enrollment candidates or expected_peer.ip; it does
 * not perform network scanning. If the
 * modern identity endpoint is absent but an HTTP server answers on '/', the
 * result is LEGACY_REACHABLE so the Gateway UI can distinguish the old KORVO
 * firmware from a dead address. */
esp_err_t gateway_discovery_probe_ip(const char *ip, gateway_discovery_result_t *result);
const char *gateway_discovery_state_name(gateway_discovery_state_t state);
size_t gateway_discovery_result_json(const gateway_discovery_result_t *result,
                                     char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
