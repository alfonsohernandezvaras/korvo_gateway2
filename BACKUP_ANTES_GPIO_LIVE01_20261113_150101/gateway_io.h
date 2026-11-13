#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Gateway V1 fixed GPIO contract - WT32-ETH01 / LAN8720 */
#define GW_GPIO_WIEGAND_D0_IN   35
#define GW_GPIO_WIEGAND_D1_IN   36
#define GW_GPIO_WIEGAND_D0_OUT   5 /* RXD serigrafia, lector 2 D0 */
#define GW_GPIO_WIEGAND_D1_OUT  17 /* TXD serigrafia, lector 2 D1 */
#define GW_GPIO_REX               4
#define GW_GPIO_DOOR_SENSOR      32
#define GW_GPIO_CALL_BUTTON      39
#define GW_GPIO_DOOR_RELAY       33
#define GW_GPIO_BEACON           14

#define GW_GPIO_RESERVED_1        2
#define GW_GPIO_RESERVED_2       15
#define GW_GPIO_RESERVED_LAST    12

typedef struct {
    bool rex_active_low;
    bool door_open_high;
    bool call_active_low;
    bool relay_active_high;
    bool beacon_active_high;
    uint32_t call_long_press_ms;
    uint32_t wiegand_frame_gap_ms;
} gateway_io_config_t;

typedef struct {
    bool rex_active;
    bool door_open;
    bool call_pressed;
    bool relay_on;
    bool beacon_on;
    uint32_t last_wiegand_bits;
    uint64_t last_wiegand_raw;
} gateway_io_status_t;

esp_err_t gateway_io_init(const gateway_io_config_t *config);
gateway_io_config_t gateway_io_default_config(void);
void gateway_io_get_status(gateway_io_status_t *status);

esp_err_t gateway_door_unlock(uint32_t ms);
esp_err_t gateway_door_lock(void);
esp_err_t gateway_beacon_set(bool on);
esp_err_t gateway_beacon_flash(uint32_t on_ms, uint32_t off_ms, uint32_t cycles);

esp_err_t gateway_wiegand_tx(uint8_t format, uint32_t facility, uint32_t card);

#ifdef __cplusplus
}
#endif
