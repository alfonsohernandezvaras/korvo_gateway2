#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GW_SIP_IDLE = 0,
    GW_SIP_CALLING,
    GW_SIP_RINGING,
    GW_SIP_INCOMING,
    GW_SIP_CONNECTED,
    GW_SIP_BUSY,
    GW_SIP_NO_ANSWER,
    GW_SIP_REJECTED,
    GW_SIP_FAILED,
    GW_SIP_ENDED,
} gateway_sip_state_t;

typedef enum {
    GW_CALL_SEND_RECEIVE = 0,
    GW_CALL_SEND_ONLY,
    GW_CALL_RECEIVE_ONLY,
    GW_CALL_DISABLED,
} gateway_call_mode_t;

typedef struct {
    char server[64];
    uint16_t port;
    char extension[24];
    char extension_name[64];
    char password[64];
    char central_extension[24];
    char central_name[64];
    gateway_call_mode_t call_mode;
    uint32_t call_timeout_s;
    bool beacon_on_incoming;
} gateway_sip_config_t;

typedef struct {
    bool registered;
    gateway_sip_state_t state;
    char remote_extension[24];
    char remote_name[64];
    char direction[12];
    char call_id[40];
} gateway_sip_status_t;

typedef struct {
    bool connected;
    bool audio_ready;
    char device[64];
    char profile[16];
} gateway_bt_status_t;

typedef enum {
    GW_SIP_ACTION_CALL = 0,
    GW_SIP_ACTION_ANSWER,
    GW_SIP_ACTION_REJECT,
    GW_SIP_ACTION_HANGUP,
} gateway_sip_action_t;

typedef esp_err_t (*gateway_sip_action_handler_t)(gateway_sip_action_t action, void *ctx);

void gateway_services_init(void);
const char *gateway_sip_state_name(gateway_sip_state_t state);
const char *gateway_call_mode_name(gateway_call_mode_t mode);

void gateway_sip_set_config(const gateway_sip_config_t *config);
void gateway_sip_get_config(gateway_sip_config_t *config);
void gateway_sip_register_action_handler(gateway_sip_action_handler_t handler, void *ctx);
esp_err_t gateway_sip_request(gateway_sip_action_t action);

void gateway_sip_set_registered(bool registered, bool simulated);
void gateway_sip_set_state(gateway_sip_state_t state, const char *direction,
                           const char *extension, const char *name,
                           const char *call_id, bool simulated);
void gateway_sip_get_status(gateway_sip_status_t *status);

void gateway_bt_set_state(bool connected, const char *device, const char *profile, bool simulated);
void gateway_audio_set_ready(bool ready, bool simulated);
void gateway_bt_get_status(gateway_bt_status_t *status);

#ifdef __cplusplus
}
#endif
