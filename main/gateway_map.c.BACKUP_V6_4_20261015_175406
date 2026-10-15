#include "gateway_map.h"
#include "gateway_io.h"
#include "korvo_protocol.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

/* The WT32-ETH01 pin contract is intentionally immutable in this editor.
 * A future board requires its own project/firmware, not an unsafe runtime pin remap. */
typedef struct { const char *label; int gpio; uint8_t word; bool required; } row_t;
static const row_t ROWS[GW_MAP_ROWS] = {
    {"REX / solicitud salida", GW_GPIO_REX, KORVO_MSG_REX, true},
    {"Puerta cerrada", GW_GPIO_DOOR_SENSOR, KORVO_MSG_DOOR_LOCKED, true},
    {"Puerta abierta", GW_GPIO_DOOR_SENSOR, KORVO_MSG_DOOR_OPENED, true},
    {"Pulsador llamada", GW_GPIO_CALL_BUTTON, KORVO_MSG_CALL_BUTTON, true},
    {"Rele apertura", GW_GPIO_DOOR_RELAY, KORVO_MSG_DOOR_OPEN, true},
    {"Baliza", GW_GPIO_BEACON, 0, false},
    {"Wiegand D0 RX", GW_GPIO_WIEGAND_D0_IN, 0, false},
    {"Wiegand D1 RX", GW_GPIO_WIEGAND_D1_IN, 0, false},
    {"Wiegand D0 TX", GW_GPIO_WIEGAND_D0_OUT, 0, false},
    {"Wiegand D1 TX", GW_GPIO_WIEGAND_D1_OUT, 0, false}
};
static const char *NS = "gw_gpio_map";

const char *gateway_map_label(unsigned i) { return i < GW_MAP_ROWS ? ROWS[i].label : ""; }
int gateway_map_gpio(unsigned i) { return i < GW_MAP_ROWS ? ROWS[i].gpio : -1; }
uint8_t gateway_map_default_word(unsigned i) { return i < GW_MAP_ROWS ? ROWS[i].word : 0; }
bool gateway_map_required(unsigned i) { return i < GW_MAP_ROWS && ROWS[i].required; }

static void defaults(gateway_map_config_t *c)
{
    memset(c, 0, sizeof(*c));
    for (unsigned i=0; i<GW_MAP_ROWS; ++i) {
        c->word[i] = ROWS[i].word;
        c->enabled[i] = ROWS[i].required;
    }
}

void gateway_map_get(gateway_map_config_t *out)
{
    if (!out) return;
    defaults(out);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t version=0;
    if (nvs_get_u8(h,"version",&version) == ESP_OK && version == 1) {
        uint8_t saved=0;
        if (nvs_get_u8(h,"saved",&saved) == ESP_OK && saved == 1) {
            size_t n=GW_MAP_ROWS;
            uint8_t words[GW_MAP_ROWS];
            uint16_t mask=0;
            if (nvs_get_blob(h,"words",words,&n) == ESP_OK && n == GW_MAP_ROWS &&
                nvs_get_u16(h,"enabled",&mask) == ESP_OK) {
                memcpy(out->word,words,GW_MAP_ROWS);
                for (unsigned i=0;i<GW_MAP_ROWS;++i) out->enabled[i] = (mask & (1u<<i)) != 0;
                (void)nvs_get_u32(h,"revision",&out->revision);
                out->saved=true;
            }
        }
    }
    nvs_close(h);
}

void gateway_map_validation(const gateway_map_config_t *c, bool *ok, unsigned *percent)
{
    unsigned valid=0, total=0;
    bool unique=true;
    if (!c) { if(ok)*ok=false; if(percent)*percent=0; return; }
    for (unsigned i=0;i<GW_MAP_ROWS;++i) {
        if (ROWS[i].required) {
            ++total;
            if (c->enabled[i] && c->word[i] == ROWS[i].word) ++valid;
        }
        if (c->enabled[i] && c->word[i] &&
            (!korvo_protocol_is_valid(c->word[i]) || c->word[i] != ROWS[i].word)) unique=false;
        /* Optional physical channels have no corresponding word in dictionary V1. */
        if (!ROWS[i].required && c->word[i]) unique=false;
    }
    if (percent) *percent = total ? valid * 100 / total : 0;
    if (ok) *ok = c->saved && unique && valid == total;
}

esp_err_t gateway_map_save(const gateway_map_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    for (unsigned i=0;i<GW_MAP_ROWS;++i) {
        if (ROWS[i].required && cfg->word[i] != ROWS[i].word)
            return ESP_ERR_INVALID_ARG;
        if (!ROWS[i].required && cfg->word[i] != 0) return ESP_ERR_INVALID_ARG;
    }
    gateway_map_config_t previous;
    gateway_map_get(&previous);
    nvs_handle_t h;
    esp_err_t e=nvs_open(NS,NVS_READWRITE,&h);
    if(e!=ESP_OK) return e;
    uint16_t mask=0;
    for(unsigned i=0;i<GW_MAP_ROWS;++i) if(cfg->enabled[i]) mask |= (uint16_t)(1u<<i);
    uint32_t rev=previous.revision+1;
    if(e==ESP_OK)e=nvs_set_u8(h,"version",1);
    if(e==ESP_OK)e=nvs_set_blob(h,"words",cfg->word,GW_MAP_ROWS);
    if(e==ESP_OK)e=nvs_set_u16(h,"enabled",mask);
    if(e==ESP_OK)e=nvs_set_u32(h,"revision",rev);
    if(e==ESP_OK)e=nvs_set_u8(h,"saved",1);
    if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h);
    return e;
}

size_t gateway_map_status_json(char *out, size_t len)
{
    if(!out || !len)return 0;
    gateway_map_config_t c;
    gateway_map_get(&c);
    bool ok=false;
    unsigned pct=0;
    gateway_map_validation(&c,&ok,&pct);
    int n=snprintf(out,len,"{\"ok\":%s,\"percent\":%u,\"revision\":%lu,\"saved\":%s}",
       ok?"true":"false",pct,(unsigned long)c.revision,c.saved?"true":"false");
    return n<0?0:(size_t)n<len?(size_t)n:len-1;
}
