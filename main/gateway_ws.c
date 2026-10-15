#include "gateway_ws.h"
#include "gateway_enrollment.h"
#include "gateway_events.h"
#include "gateway_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "gw_ws";

static httpd_handle_t s_server;
static int s_fd = -1;
static bool s_authenticated;
static char s_source_ip[16];
static SemaphoreHandle_t s_lock;

static void lock(void) { if (s_lock) xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

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
        return inet_ntop(AF_INET, &a6->sin6_addr.s6_addr[12], out, 16) != NULL;
    }
    return false;
}

static esp_err_t send_text_req(httpd_req_t *req, const char *text)
{
    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)(text ? text : "{}");
    frame.len = strlen((const char *)frame.payload);
    return httpd_ws_send_frame(req, &frame);
}

typedef struct {
    httpd_handle_t server;
    int fd;
    char text[768];
} ws_work_t;

static void send_work(void *arg)
{
    ws_work_t *w = (ws_work_t *)arg;
    if (!w) return;
    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)w->text;
    frame.len = strlen(w->text);
    esp_err_t err = httpd_ws_send_frame_async(w->server, w->fd, &frame);
    if (err != ESP_OK) {
        bool peer_lost = false;
        lock();
        if (s_fd == w->fd) {
            s_fd = -1;
            s_authenticated = false;
            s_source_ip[0] = '\0';
            peer_lost = true;
        }
        unlock();
        if (peer_lost) gateway_enrollment_peer_offline();
    }
    free(w);
}

static void queue_text(const char *text)
{
    if (!text || !s_server) return;
    lock();
    int fd = s_fd;
    bool auth = s_authenticated;
    unlock();
    if (fd < 0 || !auth) return;
    ws_work_t *w = calloc(1, sizeof(*w));
    if (!w) return;
    w->server = s_server;
    w->fd = fd;
    snprintf(w->text, sizeof(w->text), "%s", text);
    if (httpd_queue_work(s_server, send_work, w) != ESP_OK) free(w);
}

static void event_listener(const gateway_event_t *event, void *ctx)
{
    (void)ctx;
    char json[768];
    gateway_protocol_event_json(event, json, sizeof(json));
    queue_text(json);
}

/* V2.5 pairing is deliberately simple:
 *   Gateway stores one KORVO IP.
 *   KORVO stores one Gateway IP and opens this WebSocket to it.
 * If the TCP source IP equals the configured KORVO IP, the session is admitted.
 * MAC/hostname/location/Pair Key are not authorization inputs. */
static bool begin_session(httpd_req_t *req)
{
    gateway_enrollment_status_t e;
    gateway_enrollment_get_status(&e);
    if (!gateway_enrollment_has_expected_peer() || !e.expected_peer.ip[0]) return false;

    char src[16] = {0};
    if (!request_ipv4(req, src) || strcmp(src, e.expected_peer.ip) != 0) {
        ESP_LOGW(TAG, "WS rejected: source IP %s, expected KORVO %s", src, e.expected_peer.ip);
        return false;
    }

    lock();
    s_fd = httpd_req_to_sockfd(req);
    s_authenticated = true;
    snprintf(s_source_ip, sizeof(s_source_ip), "%s", src);
    unlock();

    gateway_identity_t peer = e.observed_peer;
    snprintf(peer.ip, sizeof(peer.ip), "%s", src);
    gateway_enrollment_peer_seen(&peer, true);

    char hello[900];
    gateway_protocol_hello_json(hello, sizeof(hello));
    if (send_text_req(req, hello) != ESP_OK) return false;
    char status[4096];
    gateway_protocol_status_json(status, sizeof(status));
    if (send_text_req(req, status) != ESP_OK) return false;

    ESP_LOGI(TAG, "KORVO control session admitted from %s", src);
    return true;
}

static uint32_t json_u32(cJSON *obj, const char *key, uint32_t def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(v) && v->valuedouble >= 0) return (uint32_t)v->valuedouble;
    return def;
}

static esp_err_t handle_command(httpd_req_t *req, cJSON *root)
{
    cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsString(n) || !n->valuestring) return ESP_ERR_INVALID_ARG;
    const char *name = n->valuestring;

    if (!strcmp(name, "sys.status")) {
        char status[4096];
        gateway_protocol_status_json(status, sizeof(status));
        return send_text_req(req, status);
    }
    if (!strcmp(name, "sys.ping")) {
        esp_err_t pong = send_text_req(req, "{\"v\":1,\"type\":\"event\",\"name\":\"sys.pong\",\"data\":{}}");
        if (pong != ESP_OK) return pong;
        char status[4096];
        gateway_protocol_status_json(status, sizeof(status));
        return send_text_req(req, status);
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    uint32_t a = 0, b = 0, c = 0;
    if (cJSON_IsObject(data)) {
        if (!strcmp(name, "door.unlock")) a = json_u32(data, "ms", 5000);
        else if (!strcmp(name, "beacon.flash")) {
            a = json_u32(data, "on_ms", 500); b = json_u32(data, "off_ms", 500); c = json_u32(data, "cycles", 5);
        } else if (!strcmp(name, "wiegand.tx")) {
            a = json_u32(data, "format", 26); b = json_u32(data, "facility", 0); c = json_u32(data, "card", 0);
        }
    }
    char ack[320];
    gateway_protocol_command(name, a, b, c, ack, sizeof(ack));
    return send_text_req(req, ack);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        return begin_session(req) ? ESP_OK : ESP_FAIL;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.len == 0 || frame.len > 4096) return ESP_ERR_INVALID_SIZE;
    uint8_t *payload = calloc(1, frame.len + 1);
    if (!payload) return ESP_ERR_NO_MEM;
    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) { free(payload); return err; }
    payload[frame.len] = '\0';

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        lock(); s_fd = -1; s_authenticated = false; s_source_ip[0] = '\0'; unlock();
        gateway_enrollment_peer_offline();
        free(payload);
        return ESP_OK;
    }
    if (frame.type != HTTPD_WS_TYPE_TEXT) { free(payload); return ESP_OK; }

    cJSON *root = cJSON_Parse((const char *)payload);
    free(payload);
    if (!root) return send_text_req(req, "{\"v\":1,\"type\":\"ack\",\"name\":\"parse\",\"ok\":false,\"error\":\"invalid_json\"}");

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    lock(); bool authenticated = s_authenticated; unlock();
    if (!authenticated) {
        cJSON_Delete(root);
        return send_text_req(req, "{\"v\":1,\"type\":\"ack\",\"name\":\"protocol\",\"ok\":false,\"error\":\"ip_pair_required\"}");
    }

    if (cJSON_IsString(type) && !strcmp(type->valuestring, "cmd")) {
        err = handle_command(req, root);
    } else {
        err = send_text_req(req, "{\"v\":1,\"type\":\"ack\",\"name\":\"protocol\",\"ok\":false,\"error\":\"unsupported_message\"}");
    }
    cJSON_Delete(root);
    return err;
}

esp_err_t gateway_ws_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_server = server;
    gateway_events_add_listener(event_listener, NULL);

    static const httpd_uri_t uri = {
        .uri = "/ws/korvo",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true,
    };
    esp_err_t err = httpd_register_uri_handler(server, &uri);
    if (err == ESP_OK) ESP_LOGI(TAG, "KORVO WebSocket ready at /ws/korvo (IP pair mode)");
    return err;
}
