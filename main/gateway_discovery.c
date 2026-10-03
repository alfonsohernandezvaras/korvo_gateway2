#include "gateway_discovery.h"

#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "cJSON.h"

static const char *TAG = "gw_discovery";

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} response_buf_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (!evt || !evt->user_data) return ESP_OK;
    response_buf_t *r = (response_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0 && r->buf && r->cap > 1) {
        size_t room = r->cap - r->len - 1;
        size_t take = (size_t)evt->data_len < room ? (size_t)evt->data_len : room;
        if (take) {
            memcpy(r->buf + r->len, evt->data, take);
            r->len += take;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

static bool valid_ipv4(const char *ip)
{
    struct in_addr addr;
    return ip && ip[0] && inet_pton(AF_INET, ip, &addr) == 1;
}

static esp_err_t http_get(const char *url, char *body, size_t body_len, int *status)
{
    response_buf_t rb = {.buf = body, .cap = body_len, .len = 0};
    if (body && body_len) body[0] = '\0';
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 1800,
        .buffer_size = 512,
        .event_handler = http_event,
        .user_data = &rb,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;
    esp_http_client_set_method(c, HTTP_METHOD_GET);
    esp_http_client_set_header(c, "Connection", "close");
    esp_err_t err = esp_http_client_perform(c);
    if (status) *status = (err == ESP_OK) ? esp_http_client_get_status_code(c) : 0;
    esp_http_client_cleanup(c);
    return err;
}

static void json_copy(cJSON *root, const char *key, char *dst, size_t len)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(v) && v->valuestring) snprintf(dst, len, "%s", v->valuestring);
}

const char *gateway_discovery_state_name(gateway_discovery_state_t state)
{
    switch (state) {
        case GW_DISCOVERY_LEGACY_REACHABLE: return "legacy_reachable";
        case GW_DISCOVERY_IDENTITY_OK: return "identity_ok";
        case GW_DISCOVERY_INVALID_IDENTITY: return "invalid_identity";
        case GW_DISCOVERY_NOT_FOUND:
        default: return "not_found";
    }
}

esp_err_t gateway_discovery_probe_ip(const char *ip, gateway_discovery_result_t *result)
{
    if (!result || !valid_ipv4(ip)) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    result->state = GW_DISCOVERY_NOT_FOUND;
    snprintf(result->identity.ip, sizeof(result->identity.ip), "%s", ip);

    char url[96];
    char body[1400];
    int status = 0;
    snprintf(url, sizeof(url), "http://%s/api/device/identity", ip);
    esp_err_t err = http_get(url, body, sizeof(body), &status);
    result->http_status = status;

    if (err == ESP_OK && status == 200 && body[0]) {
        cJSON *root = cJSON_Parse(body);
        if (root) {
            json_copy(root, "role", result->role, sizeof(result->role));
            json_copy(root, "name", result->identity.device_name, sizeof(result->identity.device_name));
            json_copy(root, "hostname", result->identity.hostname, sizeof(result->identity.hostname));
            json_copy(root, "location", result->identity.location, sizeof(result->identity.location));
            json_copy(root, "ip", result->identity.ip, sizeof(result->identity.ip));
            json_copy(root, "mac", result->identity.mac, sizeof(result->identity.mac));
            json_copy(root, "firmware", result->firmware, sizeof(result->firmware));
            cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "protocol");
            if (cJSON_IsNumber(p) && p->valuedouble >= 0) result->protocol = (unsigned)p->valuedouble;
            bool role_ok = strcmp(result->role, "korvo") == 0;
            /* V2.5 verification is IP/role based. The HTTP connection already
             * reached the configured target IP; published labels and MAC are
             * informational only and may be blank. */
            snprintf(result->identity.ip, sizeof(result->identity.ip), "%s", ip);
            result->state = role_ok ? GW_DISCOVERY_IDENTITY_OK : GW_DISCOVERY_INVALID_IDENTITY;
            snprintf(result->message, sizeof(result->message), "%s",
                     result->state == GW_DISCOVERY_IDENTITY_OK ? "KORVO answered at configured IP" : "Identity endpoint answered but role is not KORVO");
            cJSON_Delete(root);
            return ESP_OK;
        }
        /* Some legacy webservers return the normal HTML page with status 200
         * for unknown paths. Fall through to the root reachability probe. */
    }

    /* Old KORVO firmware has a webserver but no identity endpoint. Probe '/'
     * so the UI can show REACHABLE / LEGACY instead of OFFLINE. */
    status = 0;
    snprintf(url, sizeof(url), "http://%s/", ip);
    err = http_get(url, NULL, 0, &status);
    result->http_status = status;
    if (err == ESP_OK && status >= 200 && status < 500) {
        result->state = GW_DISCOVERY_LEGACY_REACHABLE;
        snprintf(result->message, sizeof(result->message), "HTTP reachable; KORVO identity API not available (legacy firmware)");
        ESP_LOGI(TAG, "Legacy HTTP device reachable at %s (status %d)", ip, status);
        return ESP_OK;
    }

    result->state = GW_DISCOVERY_NOT_FOUND;
    snprintf(result->message, sizeof(result->message), "No HTTP device responded at the requested peer IP");
    return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
}

size_t gateway_discovery_result_json(const gateway_discovery_result_t *r, char *out, size_t out_len)
{
    if (!r || !out || !out_len) return 0;
    int n = snprintf(out, out_len,
        "{\"ok\":%s,\"state\":\"%s\",\"http_status\":%d,\"message\":\"%s\","
        "\"identity\":{\"role\":\"%s\",\"name\":\"%s\",\"hostname\":\"%s\",\"location\":\"%s\","
        "\"ip\":\"%s\",\"mac\":\"%s\",\"protocol\":%u,\"firmware\":\"%s\"}}",
        r->state != GW_DISCOVERY_NOT_FOUND ? "true" : "false", gateway_discovery_state_name(r->state),
        r->http_status, r->message, r->role, r->identity.device_name, r->identity.hostname,
        r->identity.location, r->identity.ip, r->identity.mac, r->protocol, r->firmware);
    if (n < 0) return 0;
    return (size_t)n < out_len ? (size_t)n : out_len - 1;
}
