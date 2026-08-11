#include "settings_server.h"
#include "storage.h"
#include "http_forms.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_err.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "settings_server";
static const char *NVS_NAMESPACE = "helios_mini";

#define SETTINGS_MAX_FORM_LEN 512
#define SETTINGS_HA_URL_LEN   128
#define SETTINGS_HA_TOKEN_LEN 256

bool settings_get_ha_config(char *url_out, size_t url_len, char *token_out, size_t token_len)
{
    url_out[0] = '\0';
    token_out[0] = '\0';
    bool have_url = storage_get_string(NVS_NAMESPACE, "ha_url", url_out, url_len) == ESP_OK && url_out[0] != '\0';
    bool have_token = storage_get_string(NVS_NAMESPACE, "ha_token", token_out, token_len) == ESP_OK && token_out[0] != '\0';
    return have_url && have_token;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char url[SETTINGS_HA_URL_LEN] = {0};
    char token[SETTINGS_HA_TOKEN_LEN] = {0};
    bool configured = settings_get_ha_config(url, sizeof(url), token, sizeof(token));

    char escaped_url[SETTINGS_HA_URL_LEN + 40];
    http_form_html_escape(url, escaped_url, sizeof(escaped_url));

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Helios Mini settings</title>"
        "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em;"
        "background:#0b0b0f;color:#eee}h1{color:#efb428}"
        "input,button{width:100%;padding:.6em;margin:.4em 0;box-sizing:border-box;"
        "border-radius:6px;border:1px solid #444;background:#1a1a22;color:#eee;font-size:1em}"
        "button{background:#efb428;color:#111;font-weight:bold;border:none}"
        "label{font-size:.9em;opacity:.8}"
        ".status{font-size:.85em;opacity:.7}</style></head><body>"
        "<h1>Helios Mini</h1>");

    char status_line[96];
    snprintf(status_line, sizeof(status_line), "<p class='status'>Home Assistant: %s</p>",
             configured ? "configured &#10003;" : "not configured yet");
    httpd_resp_sendstr_chunk(req, status_line);

    httpd_resp_sendstr_chunk(req,
        "<form method='POST' action='/save'>"
        "<label>Home Assistant URL</label>"
        "<input type='text' name='ha_url' placeholder='http://192.168.0.2:8123' value='");
    httpd_resp_sendstr_chunk(req, escaped_url);
    httpd_resp_sendstr_chunk(req,
        "'>"
        "<label>Long-Lived Access Token</label>"
        "<input type='password' name='ha_token' placeholder='paste your token here'>"
        "<button type='submit'>Save</button>"
        "</form>"
        "<p class='status'>Generate one in Home Assistant: profile "
        "(bottom left) &rarr; Security &rarr; Long-Lived Access Tokens.</p>"
        "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > SETTINGS_MAX_FORM_LEN) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form too large");
        return ESP_FAIL;
    }

    char body[SETTINGS_MAX_FORM_LEN + 1] = {0};
    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read failed");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char url[SETTINGS_HA_URL_LEN] = {0};
    char token[SETTINGS_HA_TOKEN_LEN] = {0};
    http_form_get(body, "ha_url", url, sizeof(url));
    http_form_get(body, "ha_token", token, sizeof(token));

    if (url[0] != '\0') {
        storage_set_string(NVS_NAMESPACE, "ha_url", url);
    }
    if (token[0] != '\0') {
        storage_set_string(NVS_NAMESPACE, "ha_token", token);
    }
    ESP_LOGI(TAG, "settings saved (url set: %s, token set: %s)",
             url[0] ? "yes" : "no", token[0] ? "yes" : "no");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!doctype html><html><body style='font-family:sans-serif;text-align:center;"
        "margin-top:3em;background:#0b0b0f;color:#eee'>"
        "<h1 style='color:#efb428'>Saved</h1>"
        "<p><a href='/' style='color:#efb428'>Back to settings</a></p>"
        "</body></html>");
    return ESP_OK;
}

void settings_server_start(void)
{
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 4;
    /* See docs/HARDWARE_REFERENCE.md - networking/provisioning hit a stack
     * overflow on the default 4KB the moment a client connected for real. */
    http_cfg.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &http_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start settings HTTP server");
        return;
    }

    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &save_uri);

    ESP_LOGI(TAG, "settings server started");
}
