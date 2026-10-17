/* Network interface hooks: byte counting and PCAP capture.
 *
 * Hooks into the lwIP netif input/linkoutput chains for both ETH
 * and AP bridge port interfaces to intercept packets for monitoring.
 */

#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/def.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "driver/gpio.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "client_stats.h"
#include "pcap_capture.h"
#include "router_config.h"
#include "wifi_config.h"

extern esp_netif_t *eth_port_netif;
extern esp_netif_t *wifi_port_netif;
extern esp_netif_t *br_netif;

static const char *TAG = "netif_hooks";

/* G2_R8_ETH_SIP_PROBE: lectura pasiva de Ethernet ANTES de lwIP.
 * Solo copia 96 bytes, nunca altera ni retiene el pbuf. Consola en tarea separada.
 * No requiere cliente PCAP/TCP ni modifica SIP, GPIO o el bridge. */
typedef struct {
    uint32_t src, dst;
    uint16_t ipid, udp_src, udp_dst, ip_len, frame_len, frag_offset;
    uint8_t mf, has_udp, first_len;
    char first[32];
} g2_r8_pkt_t;
static QueueHandle_t g2_r8_q;
static volatile uint32_t g2_r8_drop;
static void g2_r8_console_task(void *arg) {
    (void)arg;
    g2_r8_pkt_t p;
    ESP_LOGI("g2_r8_eth", "MONITOR SIP ETHERNET ACTIVO (sin Wireshark)");
    while (1) {
        if (xQueueReceive(g2_r8_q, &p, pdMS_TO_TICKS(1000)) == pdTRUE) {
            const uint8_t *s=(const uint8_t *)&p.src, *d=(const uint8_t *)&p.dst;
            ESP_LOGI("g2_r8_eth", "ETH %u.%u.%u.%u -> %u.%u.%u.%u ipid=%u iplen=%u ethlen=%u MF=%u offset=%u UDP=%u->%u %s drop=%lu",
                s[0],s[1],s[2],s[3],d[0],d[1],d[2],d[3],
                (unsigned)p.ipid,(unsigned)p.ip_len,(unsigned)p.frame_len,
                (unsigned)p.mf,(unsigned)p.frag_offset,
                (unsigned)p.udp_src,(unsigned)p.udp_dst,p.first,(unsigned long)g2_r8_drop);
        }
    }
}
static void g2_r8_probe_eth(struct pbuf *p) {
    if (!p || !g2_r8_q || p->tot_len < 34) return;
    uint8_t b[112];
    u16_t len=p->tot_len < sizeof(b) ? p->tot_len : sizeof(b);
    if (pbuf_copy_partial(p,b,len,0)!=len) return;
    if (b[12]!=0x08 || b[13]!=0x00) return; /* Ethernet IPv4 sin VLAN */
    unsigned ihl=(b[14]&15u)*4u;
    if ((b[14]>>4)!=4 || ihl<20 || len<14+ihl || b[23]!=17) return;
    const uint8_t *ip=b+14;
    uint16_t frag=(uint16_t)((ip[6]<<8)|ip[7]);
    uint16_t offset=(uint16_t)((frag&0x1fffu)*8u);
    uint16_t sport=0,dport=0;
    if (!offset && len>=14+ihl+8) {
        const uint8_t *u=ip+ihl;
        sport=(uint16_t)((u[0]<<8)|u[1]);dport=(uint16_t)((u[2]<<8)|u[3]);
    }
    /* Kiosk 192.168.10.10 -> Gateway2 192.168.10.51, incl. fragmentos
       sin cabecera UDP. Tambien capturar SIP 5060/5062/5066. */
    bool from_kiosk=(ip[12]==192 && ip[13]==168 && ip[14]==10 && ip[15]==10);
    bool to_gateway=(ip[16]==192 && ip[17]==168 && ip[18]==10 && ip[19]==51);
    bool sip_port=(sport==5060 || sport==5062 || sport==5066 ||
                   dport==5060 || dport==5062 || dport==5066);
    if (!(sip_port || (from_kiosk && to_gateway))) return;
    g2_r8_pkt_t v={0};
    memcpy(&v.src,ip+12,4);memcpy(&v.dst,ip+16,4);
    v.ipid=(uint16_t)((ip[4]<<8)|ip[5]);
    v.ip_len=(uint16_t)((ip[2]<<8)|ip[3]);
    v.frame_len=p->tot_len;
    v.frag_offset=offset;v.mf=(frag&0x2000u)?1:0;
    v.udp_src=sport;v.udp_dst=dport;v.has_udp=!offset;
    if (!offset && len>14+ihl+8) {
        unsigned pos=14+ihl+8, k=0;
        while (pos<len && k<sizeof(v.first)-1 && b[pos]!='\r' && b[pos]!='\n') {
            unsigned char c=b[pos++];v.first[k++]=(c>=32 && c<127)?(char)c:'.';
        }
        v.first[k]=0;
    } else if (offset) {
        memcpy(v.first,"FRAGMENTO UDP",14);
    }
    if (xQueueSend(g2_r8_q,&v,0)!=pdTRUE) g2_r8_drop++;
}

// Original netif input and linkoutput function pointers (ETH port)
static netif_input_fn original_netif_input = NULL;
static netif_linkoutput_fn original_netif_linkoutput = NULL;
static struct netif *sta_netif = NULL;

// Original AP netif function pointers (WiFi AP port)
static netif_input_fn original_ap_netif_input = NULL;
static netif_linkoutput_fn original_ap_netif_linkoutput = NULL;
static struct netif *ap_netif = NULL;

// Cached local MAC addresses — unicast frames to these are consumed, not forwarded
static uint8_t bridge_mac[6];
static uint8_t ap_mac_cached[6];
static bool bridge_mac_valid = false;
static bool ap_mac_cached_valid = false;

// Check if a unicast destination MAC belongs to the bridge itself
static IRAM_ATTR bool is_local_unicast(const uint8_t *dst) {
    if (dst[0] & 1) return false;  // broadcast/multicast — must be forwarded
    if (bridge_mac_valid && memcmp(dst, bridge_mac, 6) == 0) return true;
    if (ap_mac_cached_valid && memcmp(dst, ap_mac_cached, 6) == 0) return true;
    return false;
}

// Per-client traffic statistics for AP clients
static client_stats_entry_t client_stats[CLIENT_STATS_MAX];

static IRAM_ATTR client_stats_entry_t* find_client_stats(const uint8_t *mac) {
    for (int i = 0; i < CLIENT_STATS_MAX; i++) {
        if (client_stats[i].active && memcmp(client_stats[i].mac, mac, 6) == 0) {
            return &client_stats[i];
        }
    }
    return NULL;
}

void client_stats_on_connect(const uint8_t *mac) {
    client_stats_entry_t *existing = find_client_stats(mac);
    if (existing) {
        existing->connected = 1;
        return;
    }
    int free_slot = -1;
    int disconnected_slot = -1;
    for (int i = 0; i < CLIENT_STATS_MAX; i++) {
        if (!client_stats[i].active) {
            free_slot = i;
            break;
        } else if (!client_stats[i].connected && disconnected_slot < 0) {
            disconnected_slot = i;
        }
    }
    int slot = (free_slot >= 0) ? free_slot : disconnected_slot;
    if (slot >= 0) {
        memcpy(client_stats[slot].mac, mac, 6);
        client_stats[slot].bytes_sent = 0;
        client_stats[slot].bytes_received = 0;
        client_stats[slot].packets_sent = 0;
        client_stats[slot].packets_received = 0;
        client_stats[slot].active = 1;
        client_stats[slot].connected = 1;
    }
}

void client_stats_on_disconnect(const uint8_t *mac) {
    client_stats_entry_t *entry = find_client_stats(mac);
    if (entry) {
        entry->connected = 0;
    }
}

int client_stats_get_all(client_stats_entry_t *out, int max_entries) {
    int count = 0;
    for (int i = 0; i < CLIENT_STATS_MAX && count < max_entries; i++) {
        if (client_stats[i].active) {
            memcpy(&out[count], &client_stats[i], sizeof(client_stats_entry_t));
            count++;
        }
    }
    return count;
}

void client_stats_reset_all(void) {
    for (int i = 0; i < CLIENT_STATS_MAX; i++) {
        if (client_stats[i].active) {
            client_stats[i].bytes_sent = 0;
            client_stats[i].bytes_received = 0;
            client_stats[i].packets_sent = 0;
            client_stats[i].packets_received = 0;
        }
    }
}

void format_bytes_human(uint64_t bytes, char *buf, size_t len) {
    if (bytes >= 1073741824ULL)
        snprintf(buf, len, "%.1f GB", (double)bytes / 1073741824.0);
    else if (bytes >= 1048576ULL)
        snprintf(buf, len, "%.1f MB", (double)bytes / 1048576.0);
    else if (bytes >= 1024ULL)
        snprintf(buf, len, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, len, "%" PRIu64 " B", bytes);
}

// ETH port input hook: count received bytes, PCAP capture
static IRAM_ATTR err_t netif_input_hook(struct pbuf *p, struct netif *netif) {
    g2_r8_probe_eth(p);
    // PCAP capture (ETH interface)
    if (pcap_should_capture(false, false)) {
        pcap_capture_packet(p);
    }

    // Count received bytes and toggle LED
    if (netif == sta_netif && p != NULL) {
        sta_bytes_received += p->tot_len;
        if (led_gpio >= 0 && ap_connect) {
            led_toggle ^= 1;
            gpio_set_level(led_gpio, led_toggle ^ led_lowactive);
        }
    }

    if (original_netif_input != NULL) {
        return original_netif_input(p, netif);
    }
    return ERR_VAL;
}

// ETH port output hook: count sent bytes, PCAP capture, filter local-destined frames
static IRAM_ATTR err_t netif_linkoutput_hook(struct netif *netif, struct pbuf *p) {
    // Drop unicast frames destined for the bridge itself — these should not
    // be forwarded out the ETH port (the bridge consumes them locally).
    if (p != NULL && p->len >= 14 && is_local_unicast((const uint8_t *)p->payload)) {
        return ERR_OK;
    }

    // PCAP capture (ETH interface)
    if (pcap_should_capture(false, false)) {
        pcap_capture_packet(p);
    }

    // Count sent bytes and toggle LED
    if (netif == sta_netif && p != NULL) {
        sta_bytes_sent += p->tot_len;
        if (led_gpio >= 0 && ap_connect) {
            led_toggle ^= 1;
            gpio_set_level(led_gpio, led_toggle ^ led_lowactive);
        }
    }

    if (original_netif_linkoutput != NULL) {
        return original_netif_linkoutput(netif, p);
    }
    return ERR_IF;
}

void init_byte_counter(void) {
    if (!g2_r8_q) {
        g2_r8_q=xQueueCreate(32,sizeof(g2_r8_pkt_t));
        if (g2_r8_q && xTaskCreate(g2_r8_console_task,"g2_r8_eth",4096,NULL,2,NULL)!=pdPASS) {
            vQueueDelete(g2_r8_q);g2_r8_q=NULL;
        }
        if (!g2_r8_q) ESP_LOGE("g2_r8_eth","No se pudo iniciar monitor");
    }

    if (eth_port_netif != NULL && original_netif_input == NULL) {
        extern struct netif *esp_netif_get_netif_impl(esp_netif_t *esp_netif);
        sta_netif = esp_netif_get_netif_impl(eth_port_netif);
        if (sta_netif != NULL) {
            original_netif_input = sta_netif->input;
            sta_netif->input = netif_input_hook;
            original_netif_linkoutput = sta_netif->linkoutput;
            sta_netif->linkoutput = netif_linkoutput_hook;
            ESP_LOGI(TAG, "Byte counter initialized for ETH interface");
        }
    }
    // Cache bridge MAC for local-destination filtering
    if (br_netif != NULL && !bridge_mac_valid) {
        if (esp_netif_get_mac(br_netif, bridge_mac) == ESP_OK) {
            bridge_mac_valid = true;
            ESP_LOGI(TAG, "Bridge MAC cached: %02x:%02x:%02x:%02x:%02x:%02x",
                     bridge_mac[0], bridge_mac[1], bridge_mac[2],
                     bridge_mac[3], bridge_mac[4], bridge_mac[5]);
        }
    }
}

uint64_t get_sta_bytes_sent(void) {
    return sta_bytes_sent;
}

uint64_t get_sta_bytes_received(void) {
    return sta_bytes_received;
}

void reset_sta_byte_counts(void) {
    sta_bytes_sent = 0;
    sta_bytes_received = 0;
}

void resync_connect_count(void) {
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        connect_count = sta_list.num;
    }
}

// Uptime functions
uint32_t get_uptime_seconds(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

void format_uptime(uint32_t seconds, char *buf, size_t buf_len) {
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;

    if (days > 0) {
        snprintf(buf, buf_len, "%lud %02lu:%02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours,
                 (unsigned long)mins, (unsigned long)secs);
    } else {
        snprintf(buf, buf_len, "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }
}

void format_boot_time(char *buf, size_t buf_len) {
    time_t now;
    time(&now);
    if (now < 100000) {
        snprintf(buf, buf_len, "unknown");
        return;
    }
    time_t boot_time = now - (time_t)get_uptime_seconds();
    struct tm timeinfo;
    localtime_r(&boot_time, &timeinfo);
    snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

// AP port input hook: per-client stats, PCAP capture
static IRAM_ATTR err_t ap_netif_input_hook(struct pbuf *p, struct netif *netif) {
    // Per-client byte counting: source MAC = client
    if (p != NULL && p->len >= 14) {
        const uint8_t *src_mac = ((const uint8_t *)p->payload) + 6;
        client_stats_entry_t *entry = find_client_stats(src_mac);
        if (entry) {
            entry->bytes_received += p->tot_len;
            entry->packets_received++;
        }
    }

    // PCAP capture (AP interface)
    if (pcap_should_capture(false, true)) {
        pcap_capture_packet(p);
    }

    if (original_ap_netif_input != NULL) {
        return original_ap_netif_input(p, netif);
    }
    return ERR_VAL;
}

// AP port output hook: per-client stats, PCAP capture, filter local-destined frames
static IRAM_ATTR err_t ap_netif_linkoutput_hook(struct netif *netif, struct pbuf *p) {
    // Drop unicast frames destined for the bridge itself — these should not
    // be forwarded out the AP port (the bridge consumes them locally).
    if (p != NULL && p->len >= 14 && is_local_unicast((const uint8_t *)p->payload)) {
        return ERR_OK;
    }

    // Per-client byte counting: dest MAC = client
    if (p != NULL && p->len >= 14) {
        const uint8_t *dst_mac = (const uint8_t *)p->payload;
        client_stats_entry_t *entry = find_client_stats(dst_mac);
        if (entry) {
            entry->bytes_sent += p->tot_len;
            entry->packets_sent++;
        }
    }

    // PCAP capture (AP interface)
    if (pcap_should_capture(false, true)) {
        pcap_capture_packet(p);
    }

    if (original_ap_netif_linkoutput != NULL) {
        return original_ap_netif_linkoutput(netif, p);
    }
    return ERR_IF;
}

void init_ap_netif_hooks(void) {
    if (wifi_port_netif != NULL && original_ap_netif_input == NULL) {
        extern struct netif *esp_netif_get_netif_impl(esp_netif_t *esp_netif);
        ap_netif = esp_netif_get_netif_impl(wifi_port_netif);

        if (ap_netif != NULL) {
            original_ap_netif_input = ap_netif->input;
            ap_netif->input = ap_netif_input_hook;

            original_ap_netif_linkoutput = ap_netif->linkoutput;
            ap_netif->linkoutput = ap_netif_linkoutput_hook;

            ESP_LOGI(TAG, "AP netif hooks initialized");
        }
    }
    // Cache AP MAC for local-destination filtering
    if (wifi_port_netif != NULL && !ap_mac_cached_valid) {
        if (esp_netif_get_mac(wifi_port_netif, ap_mac_cached) == ESP_OK) {
            ap_mac_cached_valid = true;
            ESP_LOGI(TAG, "AP MAC cached: %02x:%02x:%02x:%02x:%02x:%02x",
                     ap_mac_cached[0], ap_mac_cached[1], ap_mac_cached[2],
                     ap_mac_cached[3], ap_mac_cached[4], ap_mac_cached[5]);
        }
    }
}
