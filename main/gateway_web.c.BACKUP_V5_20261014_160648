#include "gateway_web.h"
#include "gateway_protocol.h"
#include "gateway_enrollment.h"
#include "gateway_services.h"
#include "gateway_diag.h"
#include "gateway_wifi.h"
#include "gateway_discovery.h"
#include "gateway_ws.h"
#include "http_server.h"
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
    c.port = (uint16_t)form_long(body, "port", c.port ? c.port : 5060);
    if (form_value(body, "extension", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.extension)) return send_json(req, "{\"ok\":false,\"error\":\"extension_too_long\"}"); strlcpy(c.extension, tmp, sizeof(c.extension)); }
    if (form_value(body, "extension_name", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.extension_name)) return send_json(req, "{\"ok\":false,\"error\":\"extension_name_too_long\"}"); strlcpy(c.extension_name, tmp, sizeof(c.extension_name)); }
    if (form_value(body, "password", tmp, sizeof(tmp)) && tmp[0]) { if (strlen(tmp) >= sizeof(c.password)) return send_json(req, "{\"ok\":false,\"error\":\"password_too_long\"}"); strlcpy(c.password, tmp, sizeof(c.password)); }
    if (form_value(body, "central_extension", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.central_extension)) return send_json(req, "{\"ok\":false,\"error\":\"central_extension_too_long\"}"); strlcpy(c.central_extension, tmp, sizeof(c.central_extension)); }
    if (form_value(body, "central_name", tmp, sizeof(tmp))) { if (strlen(tmp) >= sizeof(c.central_name)) return send_json(req, "{\"ok\":false,\"error\":\"central_name_too_long\"}"); strlcpy(c.central_name, tmp, sizeof(c.central_name)); }
    if (form_value(body, "call_mode", tmp, sizeof(tmp))) c.call_mode = parse_call_mode(tmp);
    c.call_timeout_s = (uint32_t)form_long(body, "timeout_s", c.call_timeout_s ? c.call_timeout_s : 30);
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

    if (!strcmp(action, "wiegand")) gateway_diag_sim_wiegand((uint8_t)form_long(body,"format",26),(uint32_t)form_long(body,"facility",0),(uint32_t)form_long(body,"card",0),"");
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
"<div class='tabs'><a href='/'>STATUS</a><a href='/config'>GATEWAY</a><a href='/sip'>SIP / VOIP</a><a href='/diagnostic'>DIAGNOSTICO</a></div>";

static esp_err_t send_page_start(httpd_req_t *req, const char *title, const char *active)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>");
    httpd_resp_sendstr_chunk(req, title);
    httpd_resp_sendstr_chunk(req, "</title>");
    httpd_resp_sendstr_chunk(req, PAGE_CSS);
    httpd_resp_sendstr_chunk(req, "</head><body><div class='shell'><div class='top'><div><div class='title'>KORVO GATEWAY</div><div class='sub'>WT32-ETH01 · Gateway V2.5.1</div></div></div>");
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
"<div class='card'><h2>SIP STATUS</h2><div id='sipStatus'></div></div><div class='card'><h2>BLUETOOTH AUDIO</h2><div id='btStatus'></div><p class='sub'>Pair/scan controls will become active when the HFP engine is attached. The status interface is already reserved.</p></div>"
"<div class='card wide'><div class='actions'><button id='save'>SAVE SIP CONFIGURATION</button></div><div id='msg' class='msg'></div></div></div>"
"<script>const $=id=>document.getElementById(id);async function load(){let r=await fetch('/api/gw/sip',{cache:'no-store'});if(!r.ok){$('msg').textContent='Authentication required';return}let d=await r.json(),c=d.config,s=d.status,b=d.bluetooth;$('server').value=c.server||'';$('port').value=c.port||5060;$('extension').value=c.extension||'';$('extension_name').value=c.extension_name||'';$('central_extension').value=c.central_extension||'';$('central_name').value=c.central_name||'';$('call_mode').value=c.call_mode;$('timeout_s').value=c.timeout_s;$('beacon').value=c.beacon_on_incoming?'1':'0';$('sipStatus').innerHTML=`<div class='row'><span class='label'>Registered</span><b class='${s.registered?'ok':'bad'}'>${s.registered?'YES':'NO'}</b></div><div class='row'><span class='label'>State</span><b>${s.state}</b></div><div class='row'><span class='label'>Remote</span><span>${s.remote_name||'-'} ${s.remote_extension||''}</span></div>`;$('btStatus').innerHTML=`<div class='row'><span class='label'>Connected</span><b class='${b.connected?'ok':'warn'}'>${b.connected?'YES':'NO'}</b></div><div class='row'><span class='label'>Device</span><span>${b.device||'-'}</span></div><div class='row'><span class='label'>Profile</span><span>${b.profile||'-'}</span></div><div class='row'><span class='label'>Audio</span><b>${b.audio_ready?'READY':'NOT READY'}</b></div>`;}$('save').onclick=async()=>{let p=new URLSearchParams({server:$('server').value,port:$('port').value,extension:$('extension').value,extension_name:$('extension_name').value,password:$('password').value,central_extension:$('central_extension').value,central_name:$('central_name').value,call_mode:$('call_mode').value,timeout_s:$('timeout_s').value,beacon_on_incoming:$('beacon').value});let r=await fetch('/api/gw/sip',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});let d=await r.json();$('msg').textContent=d.ok?'Saved to NVS':'Error: '+(d.error||'unknown');if(d.ok){$('password').value='';load();}};load();setInterval(load,3000);</script>");
    httpd_resp_sendstr_chunk(req,"</div></body></html>");
    return httpd_resp_sendstr_chunk(req,NULL);
}

static esp_err_t diag_page(httpd_req_t *req)
{
    if (!http_server_request_authenticated(req)) { httpd_resp_set_status(req,"303 See Other"); httpd_resp_set_hdr(req,"Location","/?auth_required=1"); return httpd_resp_send(req,NULL,0); }
    send_page_start(req,"Diagnostic", "diag");
    httpd_resp_sendstr_chunk(req,
"<div class='grid'>"
"<div class='card'><h2>WIEGAND SIMULATOR</h2><div class='row'><span class='label'>Format</span><select id='wf'><option>26</option><option>34</option><option>37</option></select></div><div class='row'><span class='label'>Facility</span><input id='wfac' value='101' type='number'></div><div class='row'><span class='label'>Card</span><input id='wcard' value='38724' type='number'></div><div class='actions'><button data-a='wiegand'>PRESENT CARD</button><button data-a='wiegand_tx' class='ghost'>TX PHYSICAL</button></div></div>"
"<div class='card'><h2>DIGITAL INPUTS / OUTPUTS</h2><div class='actions'><button data-a='rex_on'>REX ON</button><button data-a='rex_off' class='ghost'>REX OFF</button><button data-a='door_open'>DOOR OPEN</button><button data-a='door_closed' class='ghost'>DOOR CLOSED</button><button data-a='door_forced' class='danger'>FORCED</button><button data-a='door_held' class='danger'>HELD OPEN</button></div><div class='actions'><button data-a='call_short'>CALL SHORT</button><button data-a='call_long'>CALL LONG</button></div><div class='actions'><button data-a='relay'>RELAY PULSE</button><button data-a='beacon_on'>BEACON ON</button><button data-a='beacon_off' class='ghost'>BEACON OFF</button><button data-a='beacon_flash'>BEACON FLASH</button></div></div>"
"<div class='card'><h2>SIP SIMULATOR</h2><div class='row'><span class='label'>State</span><select id='ss'><option>idle</option><option>calling</option><option>ringing</option><option>incoming</option><option>connected</option><option>busy</option><option>no_answer</option><option>rejected</option><option>failed</option><option>ended</option></select></div><div class='row'><span class='label'>Direction</span><select id='sd'><option>incoming</option><option>outgoing</option></select></div><div class='row'><span class='label'>Extension</span><input id='se' value='300'></div><div class='row'><span class='label'>Name</span><input id='sn' value='Sala Control'></div><div class='actions'><button data-a='sip_state'>SEND STATE</button><button data-a='sip_registered'>REGISTERED</button><button data-a='sip_unregistered' class='ghost'>UNREGISTERED</button></div></div>"
"<div class='card'><h2>ENROLLMENT / WIFI</h2><div class='actions'><button data-a='peer_ok'>PAIR OK</button><button data-a='peer_mismatch' class='danger'>MISMATCH</button><button data-a='peer_auth_fail' class='danger'>AUTH FAIL</button><button data-a='peer_offline' class='ghost'>OFFLINE</button></div><div class='row'><span class='label'>RSSI dBm</span><input id='rssi' value='-52' type='number'></div><div class='row'><span class='label'>Channel</span><input id='ch' value='6' type='number'></div><div class='actions'><button data-a='wifi'>SIMULATE WIFI</button><button data-a='wifi_real' class='ghost'>REAL WIFI</button></div></div>"
"<div class='card'><h2>BLUETOOTH / AUDIO</h2><div class='row'><span class='label'>Device</span><input id='bd' value='TEST HEADSET'></div><div class='actions'><button data-a='bt_on'>BT CONNECTED</button><button data-a='bt_off' class='ghost'>BT OFF</button><button data-a='audio_ready'>AUDIO READY</button><button data-a='audio_not_ready' class='ghost'>AUDIO NOT READY</button></div></div>"
"<div class='card'><h2>LIVE STATUS</h2><pre id='live' class='mono' style='white-space:pre-wrap;font-size:12px;max-height:420px;overflow:auto'></pre></div>"
"</div><div id='msg' class='msg'></div>"
"<script>const $=i=>document.getElementById(i);async function act(a){let p=new URLSearchParams({action:a,format:$('wf').value,facility:$('wfac').value,card:$('wcard').value,state:$('ss').value,direction:$('sd').value,extension:$('se').value,name:$('sn').value,device:$('bd').value,rssi:$('rssi').value,channel:$('ch').value,ssid:'KORVO-GW-SIM',clients:'1'});let r=await fetch('/api/gw/diag',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});let d=await r.json();$('msg').textContent=d.ok?'Diagnostic action sent':'Error '+(d.error||'');setTimeout(status,100);}document.querySelectorAll('[data-a]').forEach(b=>b.onclick=()=>act(b.dataset.a));async function status(){let r=await fetch('/api/gw/status',{cache:'no-store'});let d=await r.json();$('live').textContent=JSON.stringify(d,null,2)}status();setInterval(status,2000);</script>");
    httpd_resp_sendstr_chunk(req,"</div></body></html>");
    return httpd_resp_sendstr_chunk(req,NULL);
}

static const httpd_uri_t URI_STATUS = {.uri="/api/gw/status",.method=HTTP_GET,.handler=status_api};
static const httpd_uri_t URI_IDENTITY = {.uri="/api/device/identity",.method=HTTP_GET,.handler=identity_api};
static const httpd_uri_t URI_VERIFY = {.uri="/api/gw/verify",.method=HTTP_POST,.handler=verify_enrolled_post};
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
    const httpd_uri_t *uris[] = {&URI_STATUS,&URI_IDENTITY,&URI_VERIFY,&URI_ENROLL,&URI_SIP_PAGE,&URI_SIP_GET,&URI_SIP_POST,&URI_DIAG_PAGE,&URI_DIAG_POST};
    for (size_t i=0;i<sizeof(uris)/sizeof(uris[0]);++i) {
        esp_err_t r=httpd_register_uri_handler(server,uris[i]);
        if (r!=ESP_OK) { ESP_LOGE(TAG,"Failed URI %s: %s",uris[i]->uri,esp_err_to_name(r)); e=r; }
    }
    esp_err_t ws = gateway_ws_register(server);
    if (ws != ESP_OK) { ESP_LOGE(TAG,"Failed KORVO WebSocket: %s", esp_err_to_name(ws)); e = ws; }
    return e;
}
