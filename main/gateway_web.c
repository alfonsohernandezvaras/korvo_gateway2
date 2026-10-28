#include "gateway_web.h"
#include "gateway_rtp_monitor.h"
#include "gateway_protocol.h"
#include "gateway_map.h"
#include "gateway_enrollment.h"
#include "gateway_services.h"
#include "gateway_diag.h"
#include "gateway_events.h"
#include "gateway_wifi.h"
#include "gateway_discovery.h"
#include "gateway_ws.h"
#include "http_server.h"
#include "nvs.h"
#include "router_config.h"
#include "remote_console.h"
#include "pcap_capture.h"
#include "cmd_router.h"

#include "wifi_config.h"
#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "gw_web";
static int64_t s_enroll_deadline_us;

static esp_err_t require_auth(httpd_req_t *req)
{
    if (http_server_request_authenticated(req)) return ESP_OK;
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"authentication_required\"}");
    return ESP_FAIL;
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json ? json : "{}");
}

static bool recv_form(httpd_req_t *req, char *buf, size_t len)
{
    if (!buf || len < 2 || req->content_len <= 0 || (size_t)req->content_len >= len) return false;
    int off = 0;
    while (off < req->content_len) {
        int r = httpd_req_recv(req, buf + off, req->content_len - off);
        if (r <= 0) return false;
        off += r;
    }
    buf[off] = '\0';
    return true;
}

static bool form_value(const char *body, const char *key, char *out, size_t out_len)
{
    if (!body || !key || !out || !out_len) return false;
    out[0] = '\0';
    if (httpd_query_key_value(body, key, out, out_len) != ESP_OK) return false;
    /* application/x-www-form-urlencoded: + representa espacio y %HH un byte UTF-8. */
    char *src = out, *dst = out;
    while (*src) {
        if (*src == '+') { *dst++ = ' '; ++src; }
        else if (src[0] == '%' && isxdigit((unsigned char)src[1]) && src[1] &&
                 src[2] && isxdigit((unsigned char)src[2])) {
            unsigned int val = 0;
            sscanf(src + 1, "%2x", &val);
            if (val == 0) return false;
            *dst++ = (char)val; src += 3;
        } else { *dst++ = *src++; }
    }
    *dst = '\0';
    return true;
}

static long form_long(const char *body, const char *key, long def)
{
    char tmp[32];
    if (!form_value(body, key, tmp, sizeof(tmp)) || !tmp[0]) return def;
    char *end = NULL;
    long v = strtol(tmp, &end, 10);
    return (end && end != tmp) ? v : def;
}

static esp_err_t status_api(httpd_req_t *req)
{
    char json[4096];
    gateway_protocol_status_json(json, sizeof(json));
    return send_json(req, json);
}

static bool valid_ipv4_text(const char *ip)
{
    struct in_addr a;
    return ip && ip[0] && inet_pton(AF_INET, ip, &a) == 1;
}


/* GATEWAY_UNIFIED_V5_20261014 - network fields committed in one NVS transaction.
 * Identity/pairing use their existing gwv2 namespace; these are not an atomic
 * cross-namespace transaction. No reboot on validation or network commit error. */
static void gw_decode(char *s)
{
    char *r=s,*w=s;
    while (*r) {
        if (*r=='+' ) { *w++=' '; r++; }
        else if (*r=='%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char h[3]={r[1],r[2],0}; *w++=(char)strtol(h,NULL,16); r+=3;
        } else *w++=*r++;
    }
    *w=0;
}
static bool gw_field(const char *body,const char *key,char *dst,size_t cap)
{
    if (!form_value(body,key,dst,cap)) return false;
    gw_decode(dst); return true;
}
static bool gw_int(const char *s,int min,int max,int *out)
{
    if (!s || !*s) return false;
    char *end=NULL; long v=strtol(s,&end,10);
    if (!end || *end || v<min || v>max) return false;
    *out=(int)v; return true;
}
static bool gw_hostname_valid(const char *s)
{
    size_t n=strlen(s);
    if (!n || n>32 || s[0]=='-' || s[n-1]=='-') return false;
    for(size_t i=0;i<n;i++) if (!((s[i]>='a'&&s[i]<='z') || (s[i]>='A'&&s[i]<='Z') ||
        (s[i]>='0'&&s[i]<='9') || s[i]=='-')) return false;
    return true;
}
static bool gw_nvs_string_equals(const char *key,const char *want)
{
    char *actual=NULL;
    esp_err_t e=get_config_param_str((char *)key,&actual);
    bool same=e==ESP_OK && actual && strcmp(actual,want)==0;
    free(actual); return same;
}
static esp_err_t gw_config_all_post(httpd_req_t *req)
{
    if (require_auth(req)!=ESP_OK) return ESP_OK;
    char body[4096];
    if (!recv_form(req,body,sizeof(body))) return send_json(req,"{\"ok\":false,\"error\":\"Solicitud demasiado larga\"}");
    char ssid[96],passwd[96],host[64],name[64],location[64],ip[48],mask[48],gw[48],
         start[48],end[48],dns[48],peer[48],cc[8],channel_s[16],auth_s[16],lease_s[16],dhcp_s[8],
         enable_s[8],open_s[8],hidden_s[8],pcap_s[8],snap_s[16],rc_s[8],port_s[16],timeout_s[16],bind_ap_s[8],bind_eth_s[8];
#define FIELD(k,v) do { if (!gw_field(body,k,v,sizeof(v))) return send_json(req,"{\"ok\":false,\"error\":\"Falta un campo de configuracion\"}"); } while(0)
    FIELD("ap_ssid",ssid); FIELD("ap_password",passwd); FIELD("hostname",host);
    FIELD("local_name",name); FIELD("location",location); FIELD("peer_ip",peer);
    FIELD("staticip",ip); FIELD("subnetmask",mask); FIELD("gateway",gw);
    FIELD("dhcps_start",start); FIELD("dhcps_end",end); FIELD("dhcps_dns",dns);
    FIELD("wifi_cc",cc); FIELD("ap_channel",channel_s); FIELD("ap_auth",auth_s);
    FIELD("dhcps_lease",lease_s); FIELD("dhcps_enabled",dhcp_s);
    FIELD("ap_enable",enable_s); FIELD("ap_open",open_s); FIELD("ap_hidden",hidden_s);
    FIELD("pcap_enabled",pcap_s); FIELD("pcap_snaplen",snap_s);
    FIELD("rc_enabled",rc_s); FIELD("rc_port",port_s); FIELD("rc_timeout",timeout_s);
    FIELD("rc_bind_ap",bind_ap_s); FIELD("rc_bind_eth",bind_eth_s);
#undef FIELD
    int channel,auth,lease,dhcp,en,open,hidden,pcap,snap,rc,port,timeout,bind_ap,bind_eth;
    if (!strlen(ssid)||strlen(ssid)>32) return send_json(req,"{\"ok\":false,\"error\":\"SSID debe tener entre 1 y 32 bytes\"}");
    if (!gw_hostname_valid(host)) return send_json(req,"{\"ok\":false,\"error\":\"Hostname invalido: use letras, numeros y guiones\"}");
    if (!name[0] || !location[0]) return send_json(req,"{\"ok\":false,\"error\":\"Nombre y ubicacion son obligatorios\"}");
    if (!valid_ipv4_text(peer)) return send_json(req,"{\"ok\":false,\"error\":\"IP de KORVO invalida\"}");
    if ((ip[0] && !valid_ipv4_text(ip)) || (mask[0] && !valid_ipv4_text(mask)) ||
        (gw[0] && !valid_ipv4_text(gw)) || (start[0] && !valid_ipv4_text(start)) ||
        (end[0] && !valid_ipv4_text(end)) || (dns[0] && !valid_ipv4_text(dns)))
        return send_json(req,"{\"ok\":false,\"error\":\"Una direccion IP no es valida\"}");
    if (!gw_int(channel_s,0,13,&channel) || !gw_int(auth_s,0,2,&auth) ||
        !gw_int(lease_s,1,14400,&lease) || !gw_int(dhcp_s,0,1,&dhcp) ||
        !gw_int(enable_s,0,1,&en) || !gw_int(open_s,0,1,&open) ||
        !gw_int(hidden_s,0,1,&hidden) || strlen(cc)!=2 ||
        !gw_int(pcap_s,0,1,&pcap) || !gw_int(snap_s,64,1600,&snap) ||
        !gw_int(rc_s,0,1,&rc) || !gw_int(port_s,1,65535,&port) ||
        !gw_int(timeout_s,0,86400,&timeout) ||
        !gw_int(bind_ap_s,0,1,&bind_ap) || !gw_int(bind_eth_s,0,1,&bind_eth))
        return send_json(req,"{\"ok\":false,\"error\":\"Canal, seguridad, DHCP o pais invalido\"}");
    if (dhcp && (!ip[0] || !mask[0] || !start[0] || !end[0]))
        return send_json(req,"{\"ok\":false,\"error\":\"DHCP necesita IP estatica, mascara y rango\"}");
    if (!ip[0] && mask[0]) return send_json(req,"{\"ok\":false,\"error\":\"IP estatica vacia con mascara configurada\"}");
    if (!open && passwd[0] && (strlen(passwd)<8 || strlen(passwd)>63))
        return send_json(req,"{\"ok\":false,\"error\":\"La clave WiFi debe tener 8 a 63 caracteres\"}");
    if (!open && !passwd[0]) {
        char *old=NULL;
        if (get_config_param_str("ap_passwd",&old)==ESP_OK && old) {
            snprintf(passwd,sizeof(passwd),"%s",old);
        }
        free(old);
        if (strlen(passwd)<8 || strlen(passwd)>63)
            return send_json(req,"{\"ok\":false,\"error\":\"Ingrese una clave WiFi de 8 a 63 caracteres\"}");
    }
    if (open) passwd[0]=0;
    nvs_handle_t h;
    esp_err_t e=nvs_open(PARAM_NAMESPACE,NVS_READWRITE,&h);
    if (e!=ESP_OK) return send_json(req,"{\"ok\":false,\"error\":\"No se pudo abrir NVS\"}");
#define SAVE_S(k,v) if (e==ESP_OK) e=nvs_set_str(h,k,v)
#define SAVE_I(k,v) if (e==ESP_OK) e=nvs_set_i32(h,k,v)
    SAVE_S("ap_ssid",ssid); SAVE_S("ap_passwd",passwd); SAVE_S("hostname",host);
    SAVE_S("static_ip",ip); SAVE_S("subnet_mask",mask); SAVE_S("gateway_addr",gw);
    SAVE_S("wifi_cc",cc); SAVE_I("ap_channel",channel); SAVE_I("ap_authmode",auth);
    SAVE_I("ap_disabled",!en); SAVE_I("ap_hidden",hidden);
    SAVE_I("dhcps_enabled",dhcp); SAVE_S("dhcps_start_ip",start); SAVE_S("dhcps_end_ip",end);
    SAVE_I("dhcps_lease",lease); SAVE_S("dhcps_dns",dns);
#undef SAVE_S
#undef SAVE_I
    if (e==ESP_OK) e=nvs_commit(h);
    nvs_close(h);
    if (e!=ESP_OK) { ESP_LOGE(TAG,"Config NVS failed: %s",esp_err_to_name(e));
        return send_json(req,"{\"ok\":false,\"error\":\"No se pudo guardar la configuracion en NVS\"}"); }
    if (!gw_nvs_string_equals("hostname",host) || !gw_nvs_string_equals("ap_ssid",ssid) ||
        !gw_nvs_string_equals("static_ip",ip))
        return send_json(req,"{\"ok\":false,\"error\":\"Error verificando la configuracion guardada\"}");
    esp_err_t tags=gateway_enrollment_set_local_tags(name,location);
    if (tags!=ESP_OK) return send_json(req,"{\"ok\":false,\"error\":\"No se pudo guardar la identidad\"}");
    gateway_enrollment_status_t current; gateway_enrollment_get_status(&current);
    if (strcmp(peer,current.expected_peer.ip)!=0) {
        gateway_enrollment_config_t cfg={0};
        memcpy(cfg.expected_peer.ip, peer, strlen(peer) + 1);
        if (gateway_enrollment_set_expected_peer(&cfg)!=ESP_OK)
            return send_json(req,"{\"ok\":false,\"error\":\"No se pudo guardar la IP KORVO\"}");
    }
    /* Apply operational tools after the persistent network and identity writes. */
    pcap_set_mode(pcap ? PCAP_MODE_PROMISCUOUS : PCAP_MODE_OFF);
    if (!pcap_set_snaplen((uint16_t)snap))
        return send_json(req,"{\"ok\":false,\"error\":\"No se pudo ajustar PCAP\"}");
    uint8_t bind=(bind_ap ? RC_BIND_AP : 0) | (bind_eth ? RC_BIND_ETH : 0);
    if (!bind) bind=RC_BIND_AP;
    esp_err_t tool_err=remote_console_set_port((uint16_t)port);
    if (tool_err==ESP_OK) tool_err=remote_console_set_bind(bind);
    if (tool_err==ESP_OK) tool_err=remote_console_set_timeout((uint32_t)timeout);
    if (tool_err==ESP_OK) tool_err=rc ? remote_console_enable() : remote_console_disable();
    if (tool_err!=ESP_OK) return send_json(req,"{\"ok\":false,\"error\":\"No se pudo configurar consola remota\"}");
    extern esp_timer_handle_t restart_timer;
    e=esp_timer_start_once(restart_timer,1800000);
    if (e!=ESP_OK) return send_json(req,"{\"ok\":false,\"error\":\"Guardado, pero no se pudo programar reinicio\"}");
    return send_json(req,"{\"ok\":true,\"reboot\":true}");
}

static esp_err_t enrollment_post(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) return ESP_OK;
    char body[1024];
    if (!recv_form(req, body, sizeof(body))) return send_json(req, "{\"ok\":false,\"error\":\"invalid_body\"}");

    char action[24] = {0};
    form_value(body, "action", action, sizeof(action));
    if (!strcmp(action, "clear")) {
        gateway_enrollment_clear_expected_peer();
        return send_json(req, "{\"ok\":true}");
    }

    gateway_enrollment_status_t cur;
    gateway_enrollment_get_status(&cur);
    if (!strcmp(action, "local")) {
        char local_name[64] = {0}, location[64] = {0}, new_host[64] = {0};
        form_value(body, "local_name", local_name, sizeof(local_name));
        form_value(body, "location", location, sizeof(location));
        bool has_hostname = form_value(body, "hostname", new_host, sizeof(new_host));
        if (!local_name[0]) snprintf(local_name, sizeof(local_name), "%s", cur.local.device_name);
        if (!location[0]) snprintf(location, sizeof(location), "%s", cur.local.location);
        bool host_changed = has_hostname && strcmp(new_host, cur.local.hostname) != 0;
        if (host_changed) {
            size_t n = strlen(new_host);
            if (!n || n > 32 || new_host[0] == '-' || new_host[n-1] == '-')
                return send_json(req, "{\"ok\":false,\"error\":\"invalid_hostname\"}");
            for (size_t i=0;i<n;i++) {
                unsigned char c=(unsigned char)new_host[i];
                if (!(isalnum(c) && c < 128) && c != '-')
                    return send_json(req, "{\"ok\":false,\"error\":\"invalid_hostname\"}");
            }
            esp_err_t e = set_config_param_str("hostname", new_host);
            char *verify = NULL;
            esp_err_t v = get_config_param_str("hostname", &verify);
            bool ok = (e == ESP_OK && v == ESP_OK && verify && strcmp(verify,new_host)==0);
            free(verify);
            if (!ok) return send_json(req, "{\"ok\":false,\"error\":\"hostname_nvs_failed\"}");
        }
        esp_err_t tags = gateway_enrollment_set_local_tags(local_name, location);
        if (tags != ESP_OK) return send_json(req, "{\"ok\":false,\"error\":\"identity_nvs_failed\"}");
        if (host_changed) {
            /* Hostname de red e identidad se vuelven a leer de NVS en el arranque. */
            extern esp_timer_handle_t restart_timer;
            esp_timer_start_once(restart_timer, 1800000);
            return send_json(req, "{\"ok\":true,\"reboot\":true}");
        }
        return send_json(req, "{\"ok\":true}");
    }

    /* V2.5: pairing configuration is only the KORVO IPv4 address.
     * VERIFY is optional diagnostics; name/MAC/location never authorize I/O. */
    if (!strcmp(action, "save_ip")) {
        char peer_ip[32] = {0};
        form_value(body, "peer_ip", peer_ip, sizeof(peer_ip));
        if (!valid_ipv4_text(peer_ip)) return send_json(req, "{\"ok\":false,\"error\":\"invalid_korvo_ip\"}");

        gateway_enrollment_config_t cfg = {0};
        snprintf(cfg.expected_peer.ip, sizeof(cfg.expected_peer.ip), "%s", peer_ip);
        /* SAVE is intentionally dumb and deterministic: persist only the IP.
         * VERIFY is a separate optional diagnostic action. */
        gateway_enrollment_set_expected_peer(&cfg);
        return send_json(req, "{\"ok\":true}");
    }

    return send_json(req, "{\"ok\":false,\"error\":\"unsupported_action\"}");
}

static bool request_ipv4(httpd_req_t *req, char out[16])
{
    if (!req || !out) return false;
    out[0] = '\0';
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getpeername(fd, (struct sockaddr *)&ss, &sl) != 0) return false;
    if (ss.ss_family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&ss;
        return inet_ntop(AF_INET, &a->sin_addr, out, 16) != NULL;
    }
    if (ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&ss;
        /* ESP-IDF httpd commonly accepts IPv4 peers on an IPv6 socket as
         * ::ffff:a.b.c.d. Use the mapped IPv4 tail, matching the base server. */
        return inet_ntop(AF_INET, &a6->sin6_addr.s6_addr[12], out, 16) != NULL;
    }
    return false;
}

static void random_hex(char *out, size_t out_len, size_t bytes)
{
    static const char h[] = "0123456789ABCDEF";
    if (!out || out_len < 2) return;
    size_t need = bytes * 2;
    if (need + 1 > out_len) need = out_len - 1;
    uint32_t r = 0;
    for (size_t i = 0; i < need; ++i) {
        if ((i & 7U) == 0) r = esp_random();
        out[i] = h[(r >> ((i & 7U) * 4U)) & 0xFU];
    }
    out[need] = '\0';
}

static esp_err_t identity_api(httpd_req_t *req)
{
    gateway_enrollment_status_t e;
    gateway_enrollment_get_status(&e);
    char json[768];
    snprintf(json, sizeof(json),
        "{\"role\":\"gateway\",\"name\":\"%s\",\"hostname\":\"%s\",\"location\":\"%s\","
        "\"ip\":\"%s\",\"mac\":\"%s\",\"korvo_ip\":\"%s\",\"protocol\":1,\"firmware\":\"%s\"}",
        e.local.device_name, e.local.hostname, e.local.location, e.local.ip, e.local.mac,
        e.expected_peer.ip, GATEWAY_V2_LAYER_VERSION);
    return send_json(req, json);
}


static esp_err_t enroll_candidate_post(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) return ESP_OK;
    if (gateway_enrollment_has_expected_peer())
        return send_json(req, "{\"ok\":false,\"error\":\"already_enrolled_use_verify\"}");

    char body[384], ip[32] = {0};
    if (!recv_form(req, body, sizeof(body)) || !form_value(body, "ip", ip, sizeof(ip)) || !ip[0])
        return send_json(req, "{\"ok\":false,\"error\":\"target_ip_required\"}");

    gateway_wifi_status_t w;
    gateway_wifi_get_status(&w);
    if (w.clients != 1 || !w.candidate_visible)
        return send_json(req, "{\"ok\":false,\"error\":\"single_wifi_candidate_required\"}");

    gateway_discovery_result_t r;
    gateway_discovery_probe_ip(ip, &r);
    bool mac_match = r.state == GW_DISCOVERY_IDENTITY_OK && r.identity.mac[0] &&
                     strcasecmp(r.identity.mac, w.candidate_mac) == 0;
    if (r.state == GW_DISCOVERY_IDENTITY_OK) snprintf(r.identity.ip, sizeof(r.identity.ip), "%s", ip);

    char probe_json[1536];
    gateway_discovery_result_json(&r, probe_json, sizeof(probe_json));
    char json[2100];
    snprintf(json, sizeof(json),
             "{\"ok\":%s,\"target_ip\":\"%s\",\"wifi_candidate_mac\":\"%s\",\"wifi_mac_match\":%s,\"probe\":%s}",
             (r.state != GW_DISCOVERY_NOT_FOUND) ? "true" : "false", ip, w.candidate_mac,
             mac_match ? "true" : "false", probe_json);
    return send_json(req, json);
}

static esp_err_t verify_enrolled_post(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) return ESP_OK;
    gateway_enrollment_status_t cur;
    gateway_enrollment_get_status(&cur);
    if (!gateway_enrollment_has_expected_peer() || !cur.expected_peer.ip[0])
        return send_json(req, "{\"ok\":false,\"error\":\"korvo_ip_not_configured\"}");

    gateway_discovery_result_t r;
    esp_err_t probe = gateway_discovery_probe_ip(cur.expected_peer.ip, &r);
    if (r.state == GW_DISCOVERY_IDENTITY_OK) {
        snprintf(r.identity.ip, sizeof(r.identity.ip), "%s", cur.expected_peer.ip);
        /* HTTP verification proves the configured IP answers as KORVO. CONTROL
         * is still enabled only by a live WS connection from this same IP. */
        gateway_enrollment_peer_seen(&r.identity, false);
    } else if (probe != ESP_OK || r.state == GW_DISCOVERY_NOT_FOUND) {
        gateway_enrollment_peer_offline();
    }

    gateway_enrollment_get_status(&cur);
    char result[1536];
    gateway_discovery_result_json(&r, result, sizeof(result));
    char json[2200];
    snprintf(json, sizeof(json),
             "{\"ok\":%s,\"target_ip\":\"%s\",\"probe\":%s,"
             "\"gateway_ok\":%s,\"control_ok\":%s,\"state\":\"%s\"}",
             (r.state == GW_DISCOVERY_IDENTITY_OK) ? "true" : "false",
             cur.expected_peer.ip, result,
             cur.gateway_ok ? "true" : "false", cur.control_allowed ? "true" : "false",
             gateway_enrollment_state_name(cur.state));
    return send_json(req, json);
}

/* Future KORVO firmware can initiate the pair from its own webserver. The
 * request is accepted only when this Gateway is not already paired and the
 * request comes from the single WiFi station currently attached to the AP.
 * The Gateway then stores the KORVO identity immediately and returns its own
 * identity plus freshly generated pair credentials. */
static esp_err_t enroll_arm_post(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) return ESP_OK;
    if (gateway_enrollment_has_expected_peer())
        return send_json(req, "{\"ok\":false,\"error\":\"already_paired\"}");
    s_enroll_deadline_us = esp_timer_get_time() + 120LL * 1000000LL;
    return send_json(req, "{\"ok\":true,\"window_s\":120}");
}

static esp_err_t enroll_request_post(httpd_req_t *req)
{
    gateway_enrollment_status_t cur;
    gateway_enrollment_get_status(&cur);
    if (cur.expected_peer.device_name[0] || cur.pair_id[0])
        return send_json(req, "{\"ok\":false,\"error\":\"already_paired\"}");
    if (s_enroll_deadline_us == 0 || esp_timer_get_time() > s_enroll_deadline_us)
        return send_json(req, "{\"ok\":false,\"error\":\"enrollment_window_closed\"}");

    char body[1024];
    if (!recv_form(req, body, sizeof(body))) return send_json(req, "{\"ok\":false,\"error\":\"invalid_body\"}");
    gateway_identity_t peer = {0};
    form_value(body, "name", peer.device_name, sizeof(peer.device_name));
    form_value(body, "hostname", peer.hostname, sizeof(peer.hostname));
    form_value(body, "location", peer.location, sizeof(peer.location));
    form_value(body, "ip", peer.ip, sizeof(peer.ip));
    form_value(body, "mac", peer.mac, sizeof(peer.mac));
    if (!peer.device_name[0] || !peer.hostname[0] || !peer.location[0] || !peer.ip[0] || !peer.mac[0])
        return send_json(req, "{\"ok\":false,\"error\":\"identity_incomplete\"}");

    char src_ip[16] = {0};
    if (!request_ipv4(req, src_ip) || strcmp(src_ip, peer.ip) != 0)
        return send_json(req, "{\"ok\":false,\"error\":\"source_ip_mismatch\"}");

    gateway_wifi_status_t w;
    gateway_wifi_get_status(&w);
    if (w.clients != 1 || !w.candidate_visible)
        return send_json(req, "{\"ok\":false,\"error\":\"single_wifi_peer_required\"}");
    if (strcasecmp(w.candidate_mac, peer.mac) != 0)
        return send_json(req, "{\"ok\":false,\"error\":\"wifi_mac_mismatch\"}");

    gateway_enrollment_config_t cfg = {0};
    cfg.expected_peer = peer;
    char rid[17];
    random_hex(rid, sizeof(rid), 8);
    snprintf(cfg.pair_id, sizeof(cfg.pair_id), "PAIR-%s", rid);
    random_hex(cfg.pair_key, sizeof(cfg.pair_key), 32);
    gateway_enrollment_set_expected_peer(&cfg);
    s_enroll_deadline_us = 0;
    gateway_enrollment_peer_seen(&peer, false);
    gateway_enrollment_get_status(&cur);

    char json[1400];
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"pair_id\":\"%s\",\"pair_key\":\"%s\",\"gateway\":{"
        "\"role\":\"gateway\",\"name\":\"%s\",\"hostname\":\"%s\",\"location\":\"%s\","
        "\"ip\":\"%s\",\"mac\":\"%s\",\"protocol\":1}}",
        cfg.pair_id, cfg.pair_key, cur.local.device_name, cur.local.hostname, cur.local.location,
        cur.local.ip, cur.local.mac);
    return send_json(req, json);
}

static gateway_call_mode_t parse_call_mode(const char *s)
{
    if (!strcmp(s, "send_only")) return GW_CALL_SEND_ONLY;
    if (!strcmp(s, "receive_only")) return GW_CALL_RECEIVE_ONLY;
    if (!strcmp(s, "disabled")) return GW_CALL_DISABLED;
    return GW_CALL_SEND_RECEIVE;
}

static esp_err_t sip_api_get(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) return ESP_OK;
    gateway_sip_config_t c;
    gateway_sip_status_t s;
    gateway_bt_status_t b;
    gateway_sip_get_config(&c);
    gateway_sip_get_status(&s);
    gateway_bt_get_status(&b);
    char json[1400];
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"config\":{\"server\":\"%s\",\"port\":%u,\"extension\":\"%s\",\"extension_name\":\"%s\","
        "\"central_extension\":\"%s\",\"central_name\":\"%s\",\"call_mode\":\"%s\",\"timeout_s\":%lu,\"beacon_on_incoming\":%s},"
        "\"status\":{\"registered\":%s,\"state\":\"%s\",\"direction\":\"%s\",\"remote_extension\":\"%s\",\"remote_name\":\"%s\"},"
        "\"bluetooth\":{\"connected\":%s,\"audio_ready\":%s,\"device\":\"%s\",\"profile\":\"%s\"}}",
        c.server, c.port, c.extension, c.extension_name, c.central_extension, c.central_name,
        gateway_call_mode_name(c.call_mode), (unsigned long)c.call_timeout_s, c.beacon_on_incoming ? "true" : "false",
        s.registered ? "true" : "false", gateway_sip_state_name(s.state), s.direction, s.remote_extension, s.remote_name,
        b.connected ? "true" : "false", b.audio_ready ? "true" : "false", b.device, b.profile);
    return send_json(req, json);
}

static esp_err_t sip_api_post(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) return ESP_OK;
    char body[1536];
    if (!recv_form(req, body, sizeof(body))) return send_json(req, "{\"ok\":false,\"error\":\"invalid_body\"}");
    gateway_sip_config_t c;
    gateway_sip_get_config(&c);
    char tmp[96];
    if (form_value(body, "server", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.server)) return send_json(req, "{\"ok\":false,\"error\":\"server_too_long\"}"); strlcpy(c.server, tmp, sizeof(c.server)); }
    /* G2-03B: validar antes de convertir a uint16_t; sin cambiar el formulario existente. */
    long sip_port = form_long(body, "port", c.port ? c.port : 5060);
    if (sip_port < 1 || sip_port > 65535)
        return send_json(req, "{\"ok\":false,\"error\":\"invalid_sip_port\"}");
    c.port = (uint16_t)sip_port;
    if (form_value(body, "extension", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.extension)) return send_json(req, "{\"ok\":false,\"error\":\"extension_too_long\"}"); strlcpy(c.extension, tmp, sizeof(c.extension)); }
    if (form_value(body, "extension_name", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.extension_name)) return send_json(req, "{\"ok\":false,\"error\":\"extension_name_too_long\"}"); strlcpy(c.extension_name, tmp, sizeof(c.extension_name)); }
    if (form_value(body, "password", tmp, sizeof(tmp)) && tmp[0]) { if (strlen(tmp) >= sizeof(c.password)) return send_json(req, "{\"ok\":false,\"error\":\"password_too_long\"}"); strlcpy(c.password, tmp, sizeof(c.password)); }
    if (form_value(body, "central_extension", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.central_extension)) return send_json(req, "{\"ok\":false,\"error\":\"central_extension_too_long\"}"); strlcpy(c.central_extension, tmp, sizeof(c.central_extension)); }
    if (form_value(body, "central_name", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.central_name)) return send_json(req, "{\"ok\":false,\"error\":\"central_name_too_long\"}"); strlcpy(c.central_name, tmp, sizeof(c.central_name)); }
    if (form_value(body, "call_mode", tmp, sizeof(tmp))) c.call_mode = parse_call_mode(tmp);
    long sip_timeout = form_long(body, "timeout_s", c.call_timeout_s ? c.call_timeout_s : 30);
    if (sip_timeout < 1 || sip_timeout > 3600)
        return send_json(req, "{\"ok\":false,\"error\":\"invalid_sip_timeout\"}");
    c.call_timeout_s = (uint32_t)sip_timeout;
    c.beacon_on_incoming = form_long(body, "beacon_on_incoming", 0) != 0;
    gateway_sip_set_config(&c);
    return send_json(req, "{\"ok\":true}");
}

static gateway_sip_state_t parse_sip_state(const char *s)
{
    if (!strcmp(s, "calling")) return GW_SIP_CALLING;
    if (!strcmp(s, "ringing")) return GW_SIP_RINGING;
    if (!strcmp(s, "incoming")) return GW_SIP_INCOMING;
    if (!strcmp(s, "connected")) return GW_SIP_CONNECTED;
    if (!strcmp(s, "busy")) return GW_SIP_BUSY;
    if (!strcmp(s, "no_answer")) return GW_SIP_NO_ANSWER;
    if (!strcmp(s, "rejected")) return GW_SIP_REJECTED;
    if (!strcmp(s, "failed")) return GW_SIP_FAILED;
    if (!strcmp(s, "ended")) return GW_SIP_ENDED;
    return GW_SIP_IDLE;
}

static esp_err_t diag_api_post(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) return ESP_OK;
    char body[1536];
    if (!recv_form(req, body, sizeof(body))) return send_json(req, "{\"ok\":false,\"error\":\"invalid_body\"}");
    char action[48] = {0};
    form_value(body, "action", action, sizeof(action));

    if (!strcmp(action, "wiegand")) gateway_diag_sim_wiegand_reader((uint8_t)form_long(body,"reader",1),(uint8_t)form_long(body,"format",26),(uint32_t)form_long(body,"facility",0),(uint32_t)form_long(body,"card",0),"");
    else if (!strcmp(action, "rex_on")) gateway_diag_sim_rex(true);
    else if (!strcmp(action, "rex_off")) gateway_diag_sim_rex(false);
    else if (!strcmp(action, "door_open")) gateway_diag_sim_door(true);
    else if (!strcmp(action, "door_closed")) gateway_diag_sim_door(false);
    else if (!strcmp(action, "door_forced")) gateway_diag_sim_door_forced();
    else if (!strcmp(action, "door_held")) gateway_diag_sim_door_held_open();
    else if (!strcmp(action, "call_short")) gateway_diag_sim_call_short(150);
    else if (!strcmp(action, "call_long")) gateway_diag_sim_call_long(2500);
    else if (!strcmp(action, "sip_registered")) gateway_diag_sim_sip_registered(true);
    else if (!strcmp(action, "sip_unregistered")) gateway_diag_sim_sip_registered(false);
    else if (!strcmp(action, "sip_state")) {
        char st[32]={0}, dir[16]={0}, ext[24]={0}, name[64]={0}, cid[40]={0};
        form_value(body,"state",st,sizeof(st)); form_value(body,"direction",dir,sizeof(dir));
        form_value(body,"extension",ext,sizeof(ext)); form_value(body,"name",name,sizeof(name)); form_value(body,"call_id",cid,sizeof(cid));
        gateway_diag_sim_sip(parse_sip_state(st), dir, ext, name, cid);
    }
    else if (!strcmp(action, "bt_on")) { char dev[64]={0}; form_value(body,"device",dev,sizeof(dev)); gateway_diag_sim_bt(true,dev[0]?dev:"TEST HEADSET","HFP"); }
    else if (!strcmp(action, "bt_off")) gateway_diag_sim_bt(false,"","HFP");
    else if (!strcmp(action, "audio_ready")) gateway_diag_sim_audio(true);
    else if (!strcmp(action, "audio_not_ready")) gateway_diag_sim_audio(false);
    else if (!strcmp(action, "peer_ok") || !strcmp(action, "peer_auth_fail") || !strcmp(action, "peer_mismatch")) {
        gateway_enrollment_status_t e; gateway_enrollment_get_status(&e); gateway_identity_t p=e.expected_peer;
        if (!strcmp(action,"peer_mismatch")) { snprintf(p.device_name,sizeof(p.device_name),"WRONG-KORVO"); snprintf(p.mac,sizeof(p.mac),"02:00:00:00:FF:EE"); }
        gateway_diag_sim_enrollment(&p, strcmp(action,"peer_auth_fail") != 0);
    }
    else if (!strcmp(action, "peer_offline")) gateway_diag_sim_peer_offline();
    else if (!strcmp(action, "wifi")) {
        char ssid[33]={0}, mac[18]={0}; form_value(body,"ssid",ssid,sizeof(ssid)); form_value(body,"mac",mac,sizeof(mac));
        gateway_diag_sim_wifi(true, ssid[0]?ssid:"KORVO-GW-SIM", mac[0]?mac:"02:00:00:00:00:02", (uint8_t)form_long(body,"channel",6),(int8_t)form_long(body,"rssi",-55),(uint16_t)form_long(body,"clients",1));
    }
    else if (!strcmp(action, "wifi_real")) gateway_diag_sim_wifi(false,"","",0,-127,0);
    else if (!strcmp(action, "sim_relay")) gateway_event_emit("door.relay_on", "{}", true);
    else if (!strcmp(action, "sim_beacon_on")) gateway_event_emit("beacon.on", "{}", true);
    else if (!strcmp(action, "sim_beacon_off")) gateway_event_emit("beacon.off", "{}", true);
    else if (!strcmp(action, "sim_wiegand_tx")) gateway_event_emit("wiegand.tx_done", "{\"source\":\"simulator\"}", true);
    else if (!strcmp(action, "relay")) gateway_diag_test_relay((uint32_t)form_long(body,"ms",5000));
    else if (!strcmp(action, "beacon_on")) gateway_diag_test_beacon(true);
    else if (!strcmp(action, "beacon_off")) gateway_diag_test_beacon(false);
    else if (!strcmp(action, "beacon_flash")) gateway_diag_test_beacon_flash(500,500,6);
    else if (!strcmp(action, "wiegand_tx")) gateway_diag_test_wiegand_tx((uint8_t)form_long(body,"format",26),(uint32_t)form_long(body,"facility",0),(uint32_t)form_long(body,"card",0));
    else return send_json(req, "{\"ok\":false,\"error\":\"unknown_action\"}");

    return send_json(req, "{\"ok\":true}");
}

static const char PAGE_CSS[] =
"<style>*{box-sizing:border-box}body{margin:0;padding:18px;background:#17140f;color:#e8e5df;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
".shell{max-width:1240px;margin:auto}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:14px}.title{font-size:24px;color:#ffb300;font-weight:700}.sub{color:#999;font-size:13px}"
".tabs{display:flex;gap:8px;flex-wrap:wrap;margin:12px 0 18px}.tabs a{padding:10px 16px;border:1px solid #5a4720;border-radius:8px;color:#ddd;text-decoration:none;background:#221d14}.tabs a.active{background:#ffb300;color:#1a1510;font-weight:700}"
".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{background:#242018;border:1px solid #47381d;border-radius:12px;padding:16px}.card h2{font-size:16px;color:#ffb300;margin:0 0 12px}.row{display:grid;grid-template-columns:180px 1fr;gap:10px;padding:6px 0;border-bottom:1px solid #30291d}.row:last-child{border:0}.label{color:#96918a}.ok{color:#66d17a}.bad{color:#ff6b6b}.warn{color:#ffc857}"
"input,select{width:100%;background:#181510;color:#eee;border:1px solid #544526;border-radius:7px;padding:10px;font-size:14px}button{border:0;border-radius:8px;padding:10px 14px;background:#ffb300;color:#1a1510;font-weight:700;cursor:pointer}.danger{background:#c84d3a;color:white}.ghost{background:#393126;color:#ddd}.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}.msg{margin-top:10px;color:#ffcf66;font-size:13px}.wide{grid-column:1/-1}.bar{height:10px;background:#332b20;border-radius:6px;overflow:hidden}.fill{height:100%;background:#ffb300;width:0}.mono{font-family:ui-monospace,Consolas,monospace}@media(max-width:800px){.grid{grid-template-columns:1fr}.row{grid-template-columns:1fr}.wide{grid-column:auto}}</style>";

static const char NAV[] =
"<div class='tabs'><a href='/'>STATUS</a><a href='/config'>GATEWAY</a><a href='/sip'>SIP / VOIP</a><a href='/gpio-map'>GPIO SIMULADOR</a></div>";

static esp_err_t send_page_start(httpd_req_t *req, const char *title, const char *active)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>");
    httpd_resp_sendstr_chunk(req, title);
    httpd_resp_sendstr_chunk(req, "</title>");
    httpd_resp_sendstr_chunk(req, PAGE_CSS);
    httpd_resp_sendstr_chunk(req, "</head><body><div class='shell'><div class='top'><div><div class='title'>KORVO Gateway V2.5</div><div class='sub'>WT32-ETH01 · Access / SIP Gateway</div></div></div>");
    (void)active;
    httpd_resp_sendstr_chunk(req, NAV);
    return ESP_OK;
}

static esp_err_t sip_page(httpd_req_t *req)
{
    if (!http_server_request_authenticated(req)) { httpd_resp_set_status(req,"303 See Other"); httpd_resp_set_hdr(req,"Location","/?auth_required=1"); return httpd_resp_send(req,NULL,0); }
    send_page_start(req,"SIP / VoIP", "sip");
    httpd_resp_sendstr_chunk(req,
"<div class='grid'><div class='card'><h2>SIP ACCOUNT</h2><div class='row'><span class='label'>PBX / Server</span><input id='server'></div><div class='row'><span class='label'>Port</span><input id='port' type='number'></div><div class='row'><span class='label'>Extension</span><input id='extension'></div><div class='row'><span class='label'>Extension name</span><input id='extension_name'></div><div class='row'><span class='label'>Password</span><input id='password' type='password' placeholder='leave blank to keep current'></div></div>"
"<div class='card'><h2>CALL</h2><div class='row'><span class='label'>Central extension</span><input id='central_extension'></div><div class='row'><span class='label'>Central name</span><input id='central_name'></div><div class='row'><span class='label'>CALL button mode</span><select id='call_mode'><option value='send_receive'>SEND + RECEIVE</option><option value='send_only'>SEND ONLY</option><option value='receive_only'>RECEIVE ONLY</option><option value='disabled'>DISABLED</option></select></div><div class='row'><span class='label'>Timeout (s)</span><input id='timeout_s' type='number'></div><div class='row'><span class='label'>Beacon on incoming</span><select id='beacon'><option value='1'>YES</option><option value='0'>NO</option></select></div></div>"
"<div class='card'><h2>SIP STATUS</h2><div id='sipStatus'></div></div>"
"<div class='card'><h2>G2-14 AUDIO SIP - MONITOR RTP</h2><p>Linphone 300 &rarr; Gateway2 &rarr; navegador / Windows &rarr; Bose.</p><a href='/audio-monitor' target='_blank' style='color:#ffb300'>ABRIR MONITOR DE AUDIO</a></div>"
"<div class='card wide'><div class='actions'><button id='restore' type='button'>RESTORE DEFAULT</button><button id='save' type='button'>SAVE SIP CONFIGURATION</button></div><div id='msg' class='msg'></div></div></div>"
"<script>const $=id=>document.getElementById(id);async function load(){let r=await fetch('/api/gw/sip',{cache:'no-store'});if(!r.ok){$('msg').textContent='Authentication required';return}let d=await r.json(),c=d.config,s=d.status;$('server').value=c.server||'';$('port').value=c.port||5060;$('extension').value=c.extension||'';$('extension_name').value=c.extension_name||'';$('central_extension').value=c.central_extension||'';$('central_name').value=c.central_name||'';$('call_mode').value=c.call_mode;$('timeout_s').value=c.timeout_s;$('beacon').value=c.beacon_on_incoming?'1':'0';$('sipStatus').innerHTML=`<div class='row'><span class='label'>Registered</span><b class='${s.registered?'ok':'bad'}'>${s.registered?'YES':'NO'}</b></div><div class='row'><span class='label'>State</span><b>${s.state}</b></div><div class='row'><span class='label'>Remote</span><span>${s.remote_name||'-'} ${s.remote_extension||''}</span></div>`;}$('save').onclick=async()=>{let p=new URLSearchParams({server:$('server').value,port:$('port').value,extension:$('extension').value,extension_name:$('extension_name').value,password:$('password').value,central_extension:$('central_extension').value,central_name:$('central_name').value,call_mode:$('call_mode').value,timeout_s:$('timeout_s').value,beacon_on_incoming:$('beacon').value});let r=await fetch('/api/gw/sip',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});let d=await r.json();$('msg').textContent=d.ok?'Saved to NVS':'Error: '+(d.error||'unknown');if(d.ok){$('password').value='';}};$('restore').onclick=()=>{$('server').value='192.168.10.10';$('port').value='5060';$('extension').value='200';$('extension_name').value='KORVO';$('password').value='korvo200';$('central_extension').value='300';$('central_name').value='OPERADOR';$('call_mode').value='send_receive';$('timeout_s').value='30';$('beacon').value='1';$('msg').textContent='Defaults cargados en formulario. Presiona SAVE para guardar en NVS.';};load();</script>");
    httpd_resp_sendstr_chunk(req,"</div></body></html>");
    return httpd_resp_sendstr_chunk(req,NULL);
}

static esp_err_t diag_page(httpd_req_t *req)
{
    httpd_resp_set_status(req,"303 See Other");
    httpd_resp_set_hdr(req,"Location","/gpio-map");
    return httpd_resp_send(req,NULL,0);
}


/* V6.5.1: restaurar API de mapa eliminada accidentalmente en la fusion. */
static esp_err_t gw_map_page(httpd_req_t *req);

static esp_err_t gw_map_api_get(httpd_req_t *req)
{
    if (require_auth(req)!=ESP_OK) return ESP_OK;
    gateway_map_config_t c; gateway_map_get(&c);
    bool ok=false; unsigned pct=0; gateway_map_validation(&c,&ok,&pct);
    char b[1600]; int n=snprintf(b,sizeof(b),
      "{\"ok\":%s,\"saved\":%s,\"percent\":%u,\"revision\":%lu,\"rows\":[",
      ok?"true":"false",c.saved?"true":"false",pct,(unsigned long)c.revision);
    for(unsigned i=0;i<GW_MAP_ROWS && n>0 && n<(int)sizeof(b)-120;++i) {
        n+=snprintf(b+n,sizeof(b)-(size_t)n,
          "%s{\"id\":%u,\"gpio\":%d,\"word\":%u,\"enabled\":%s,\"required\":%s,\"label\":\"%s\"}",
          i?",":"",i,gateway_map_gpio(i),c.word[i],c.enabled[i]?"true":"false",
          gateway_map_required(i)?"true":"false",gateway_map_label(i));
    }
    if(n<0 || n>(int)sizeof(b)-3) return send_json(req,"{\"ok\":false,\"error\":\"buffer\"}");
    snprintf(b+n,sizeof(b)-(size_t)n,"]}");
    return send_json(req,b);
}
static esp_err_t gw_map_api_post(httpd_req_t *req)
{
    if (require_auth(req)!=ESP_OK) return ESP_OK;
    char body[1200];
    if (!recv_form(req,body,sizeof(body))) return send_json(req,"{\"ok\":false,\"error\":\"body\"}");
    gateway_map_config_t c; gateway_map_get(&c);
    for(unsigned i=0;i<GW_MAP_ROWS;++i) {
        char key[16],value[16];
        snprintf(key,sizeof(key),"en%u",i);
        if(!form_value(body,key,value,sizeof(value))) return send_json(req,"{\"ok\":false,\"error\":\"missing_enabled\"}");
        if(strcmp(value,"0") && strcmp(value,"1")) return send_json(req,"{\"ok\":false,\"error\":\"invalid_enabled\"}");
        c.enabled[i]=value[0]=='1';
        snprintf(key,sizeof(key),"word%u",i);
        if(!form_value(body,key,value,sizeof(value))) return send_json(req,"{\"ok\":false,\"error\":\"missing_word\"}");
        char *end=NULL; unsigned long v=strtoul(value,&end,10);
        if(end==value || *end || v>255) return send_json(req,"{\"ok\":false,\"error\":\"invalid_word\"}");
        c.word[i]=(uint8_t)v;
    }
    esp_err_t e=gateway_map_save(&c);
    if(e!=ESP_OK) return send_json(req,"{\"ok\":false,\"error\":\"invalid_map_or_nvs\"}");
    return gw_map_api_get(req);
}

#include "gateway_gpio_v64_page.inc"
static const httpd_uri_t URI_MAP_PAGE = {.uri="/gpio-map",.method=HTTP_GET,.handler=gw_map_page};
static const httpd_uri_t URI_MAP_GET = {.uri="/api/gw/map",.method=HTTP_GET,.handler=gw_map_api_get};
static const httpd_uri_t URI_MAP_POST = {.uri="/api/gw/map",.method=HTTP_POST,.handler=gw_map_api_post};

static const httpd_uri_t URI_STATUS = {.uri="/api/gw/status",.method=HTTP_GET,.handler=status_api};
static const httpd_uri_t URI_IDENTITY = {.uri="/api/device/identity",.method=HTTP_GET,.handler=identity_api};
static const httpd_uri_t URI_VERIFY = {.uri="/api/gw/verify",.method=HTTP_POST,.handler=verify_enrolled_post};
static const httpd_uri_t URI_CONFIG_ALL = {.uri="/api/gw/config-all",.method=HTTP_POST,.handler=gw_config_all_post};
static const httpd_uri_t URI_ENROLL = {.uri="/api/gw/enrollment",.method=HTTP_POST,.handler=enrollment_post};
static const httpd_uri_t URI_SIP_PAGE = {.uri="/sip",.method=HTTP_GET,.handler=sip_page};
static const httpd_uri_t URI_SIP_GET = {.uri="/api/gw/sip",.method=HTTP_GET,.handler=sip_api_get};
static const httpd_uri_t URI_SIP_POST = {.uri="/api/gw/sip",.method=HTTP_POST,.handler=sip_api_post};
static const httpd_uri_t URI_DIAG_PAGE = {.uri="/diagnostic",.method=HTTP_GET,.handler=diag_page};
static const httpd_uri_t URI_DIAG_POST = {.uri="/api/gw/diag",.method=HTTP_POST,.handler=diag_api_post};

esp_err_t gateway_web_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    esp_err_t e = ESP_OK;
    const httpd_uri_t *uris[] = {&URI_STATUS,&URI_IDENTITY,&URI_VERIFY,&URI_ENROLL,&URI_CONFIG_ALL,&URI_SIP_PAGE,&URI_SIP_GET,&URI_SIP_POST,&URI_DIAG_PAGE,&URI_DIAG_POST,&URI_MAP_PAGE,&URI_MAP_GET,&URI_MAP_POST};
    for (size_t i=0;i<sizeof(uris)/sizeof(uris[0]);++i) {
        esp_err_t r=httpd_register_uri_handler(server,uris[i]);
        if (r!=ESP_OK) { ESP_LOGE(TAG,"Failed URI %s: %s",uris[i]->uri,esp_err_to_name(r)); e=r; }
    }
    esp_err_t ar = gateway_rtp_monitor_register(server);
    if(ar!=ESP_OK){ESP_LOGE(TAG,"G2-14 RTP routes: %s",esp_err_to_name(ar));e=ar;}
    esp_err_t ws = gateway_ws_register(server);
    if (ws != ESP_OK) { ESP_LOGE(TAG,"Failed KORVO WebSocket: %s", esp_err_to_name(ws)); e = ws; }
    return e;
}
