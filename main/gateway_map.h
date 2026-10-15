#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define GW_MAP_ROWS 10

typedef struct {
    uint8_t word[GW_MAP_ROWS];
    bool enabled[GW_MAP_ROWS];
    uint32_t revision;
    bool saved;
} gateway_map_config_t;

void gateway_map_get(gateway_map_config_t *out);
esp_err_t gateway_map_save(const gateway_map_config_t *cfg);
void gateway_map_validation(const gateway_map_config_t *cfg, bool *ok, unsigned *percent);
size_t gateway_map_status_json(char *out, size_t len);
const char *gateway_map_label(unsigned index);
int gateway_map_gpio(unsigned index);
uint8_t gateway_map_default_word(unsigned index);
bool gateway_map_required(unsigned index);
