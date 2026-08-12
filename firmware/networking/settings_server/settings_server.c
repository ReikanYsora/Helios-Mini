#include "settings_server.h"
#include "storage.h"
#include "http_forms.h"
#include "ha_client.h"
#include "ha_discovery.h"
#include "diagnostics.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_err.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "settings_server";
static const char *NVS_NAMESPACE = "helios_mini";

#define SETTINGS_MAX_FORM_LEN     512
#define SETTINGS_HA_URL_LEN       128
#define SETTINGS_HA_TOKEN_LEN     256
#define SETTINGS_MAX_SCAN_RESULTS 8

static const char *PAGE_STYLE =
    "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em;"
    "background:#0b0b0f;color:#eee}h1{color:#efb428}h2{font-size:1.1em;color:#efb428;"
    "margin-top:1.6em}"
    "input,button{width:100%;padding:.6em;margin:.4em 0;box-sizing:border-box;"
    "border-radius:6px;border:1px solid #444;background:#1a1a22;color:#eee;font-size:1em}"
    "button{background:#efb428;color:#111;font-weight:bold;border:none}"
    "a.link-button{display:block;text-align:center;text-decoration:none}"
    "a.link-button button{pointer-events:none}"
    "label{font-size:.9em;opacity:.8}"
    ".status{font-size:.85em;opacity:.7}"
    ".status.ok{color:#7cd992;opacity:1}"
    ".status.bad{color:#e07a5f;opacity:1}"
    "a{color:#efb428}</style>";

bool settings_get_ha_config(char *url_out, size_t url_len, char *token_out, size_t token_len)
{
    url_out[0] = '\0';
    token_out[0] = '\0';
    bool have_url = storage_get_string(NVS_NAMESPACE, "ha_url", url_out, url_len) == ESP_OK && url_out[0] != '\0';
    bool have_token = storage_get_string(NVS_NAMESPACE, "ha_token", token_out, token_len) == ESP_OK && token_out[0] != '\0';
    return have_url && have_token;
}

static ha_client_status_t get_stored_status(void)
{
    char key[16] = {0};
    storage_get_string(NVS_NAMESPACE, "ha_status", key, sizeof(key));
    return ha_client_status_from_key(key);
}

static void store_status(ha_client_status_t status)
{
    storage_set_string(NVS_NAMESPACE, "ha_status", ha_client_status_key(status));
}

/* ---- / : settings form ---- */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char url[SETTINGS_HA_URL_LEN] = {0};
    char token[SETTINGS_HA_TOKEN_LEN] = {0};
    bool configured = settings_get_ha_config(url, sizeof(url), token, sizeof(token));

    /* A /scan pick arrives as "/?ha_url=..." - prefill without saving yet,
     * the user still has to press Save (no silent auto-connect). */
    char picked_url[SETTINGS_HA_URL_LEN] = {0};
    http_form_get_query_param(req, "ha_url", picked_url, sizeof(picked_url));
    if (picked_url[0] != '\0') {
        strncpy(url, picked_url, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    }

    char escaped_url[SETTINGS_HA_URL_LEN + 40];
    http_form_html_escape(url, escaped_url, sizeof(escaped_url));

    ha_client_status_t status = get_stored_status();
    const char *status_class = (status == HA_CLIENT_STATUS_OK) ? "ok"
                              : (status == HA_CLIENT_STATUS_UNKNOWN) ? "" : "bad";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Helios Mini settings</title>");
    httpd_resp_sendstr_chunk(req, PAGE_STYLE);
    httpd_resp_sendstr_chunk(req, "</head><body><h1>Helios Mini</h1>");

    char status_line[160];
    snprintf(status_line, sizeof(status_line), "<p class='status %s'>Home Assistant: %s</p>",
             status_class, configured ? ha_client_status_text(status) : "not configured yet");
    httpd_resp_sendstr_chunk(req, status_line);

    httpd_resp_sendstr_chunk(req,
        "<a class='link-button' href='/scan'><button type='button'>&#128269; Scan for Home Assistant</button></a>"
        "<form method='POST' action='/save'>"
        "<label>Home Assistant URL</label>"
        "<input type='text' name='ha_url' placeholder='http://192.168.0.2:8123' value='");
    httpd_resp_sendstr_chunk(req, escaped_url);
    httpd_resp_sendstr_chunk(req,
        "'>"
        "<label>Long-Lived Access Token</label>"
        "<input type='password' name='ha_token' placeholder='paste your token here'>"
        "<button type='submit'>Save &amp; test</button>"
        "</form>");

    if (configured) {
        httpd_resp_sendstr_chunk(req, "<a class='link-button' href='/test'><button type='button'>Test connection again</button></a>");
    }

    httpd_resp_sendstr_chunk(req,
        "<p class='status'>Generate a token in Home Assistant: profile "
        "(bottom left) &rarr; Security &rarr; Long-Lived Access Tokens. "
        "Tokens don't expire on a schedule, but Home Assistant rejects "
        "them once revoked - if the status above says \"token rejected\", "
        "generate a fresh one and paste it here again.</p>"
        "<h2>Hardware</h2>"
        "<a class='link-button' href='/debug'><button type='button'>&#128295; Debug tools</button></a>"
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

    /* Re-read what's actually stored (a save with only one field filled in
     * keeps the other one from before) and test it right away - "feedback,
     * not silence" per how this project wants things built. */
    char stored_url[SETTINGS_HA_URL_LEN] = {0};
    char stored_token[SETTINGS_HA_TOKEN_LEN] = {0};
    settings_get_ha_config(stored_url, sizeof(stored_url), stored_token, sizeof(stored_token));
    ha_client_status_t status = ha_client_test_connection(stored_url, stored_token);
    store_status(status);

    ESP_LOGI(TAG, "settings saved (url set: %s, token set: %s), test result: %s",
             url[0] ? "yes" : "no", token[0] ? "yes" : "no", ha_client_status_key(status));

    char resp[512];
    snprintf(resp, sizeof(resp),
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
        "<body style='font-family:sans-serif;text-align:center;margin-top:3em;"
        "background:#0b0b0f;color:#eee'>"
        "<h1 style='color:#efb428'>Saved</h1>"
        "<p>%s</p>"
        "<p><a href='/' style='color:#efb428'>Back to settings</a></p>"
        "</body></html>",
        ha_client_status_text(status));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

static esp_err_t test_get_handler(httpd_req_t *req)
{
    char url[SETTINGS_HA_URL_LEN] = {0};
    char token[SETTINGS_HA_TOKEN_LEN] = {0};
    settings_get_ha_config(url, sizeof(url), token, sizeof(token));
    store_status(ha_client_test_connection(url, token));

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---- /scan : mDNS discovery ---- */

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    ha_discovery_result_t *results = calloc(SETTINGS_MAX_SCAN_RESULTS, sizeof(ha_discovery_result_t));
    if (results == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int count = ha_discovery_scan(results, SETTINGS_MAX_SCAN_RESULTS, 3000);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Scanning...</title>");
    httpd_resp_sendstr_chunk(req, PAGE_STYLE);
    httpd_resp_sendstr_chunk(req, "</head><body><h1>Helios Mini</h1><p>Found on your network:</p>");

    if (count == 0) {
        httpd_resp_sendstr_chunk(req,
            "<p class='status'>No Home Assistant instance answered mDNS on this network. "
            "It may have discovery/Zeroconf disabled - enter the URL manually instead.</p>");
    }

    char escaped_name[100];
    char escaped_url[SETTINGS_HA_URL_LEN + 8];
    char encoded_url[SETTINGS_HA_URL_LEN * 3];
    char link[800];
    for (int i = 0; i < count; i++) {
        http_form_html_escape(results[i].name, escaped_name, sizeof(escaped_name));
        http_form_html_escape(results[i].url, escaped_url, sizeof(escaped_url));
        http_form_url_encode(results[i].url, encoded_url, sizeof(encoded_url));
        snprintf(link, sizeof(link),
                 "<a class='link-button' href='/?ha_url=%s'><button type='button'>%s<br><small>%s</small></button></a>",
                 encoded_url, escaped_name, escaped_url);
        httpd_resp_sendstr_chunk(req, link);
    }

    httpd_resp_sendstr_chunk(req, "<p><a href='/'>&larr; Back to settings</a></p></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    free(results);
    return ESP_OK;
}

/* ---- /debug : hardware self-tests ---- */

static void send_debug_page_head(httpd_req_t *req, const char *title)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'><title>");
    httpd_resp_sendstr_chunk(req, title);
    httpd_resp_sendstr_chunk(req, "</title>");
    httpd_resp_sendstr_chunk(req, PAGE_STYLE);
    httpd_resp_sendstr_chunk(req, "</head><body><h1>Helios Mini</h1>");
}

static esp_err_t debug_get_handler(httpd_req_t *req)
{
    diagnostics_status_t status;
    diagnostics_get_status(&status);

    send_debug_page_head(req, "Debug");

    char info[300];
    if (status.wifi_connected) {
        snprintf(info, sizeof(info),
            "<p class='status'>Uptime: %llds &middot; Free heap: %u B &middot; "
            "Free PSRAM: %u B &middot; Wi-Fi: %d dBm</p>",
            (long long)status.uptime_s, (unsigned)status.free_heap,
            (unsigned)status.free_psram, status.wifi_rssi);
    } else {
        snprintf(info, sizeof(info),
            "<p class='status'>Uptime: %llds &middot; Free heap: %u B &middot; "
            "Free PSRAM: %u B</p>",
            (long long)status.uptime_s, (unsigned)status.free_heap, (unsigned)status.free_psram);
    }
    httpd_resp_sendstr_chunk(req, info);

    httpd_resp_sendstr_chunk(req,
        "<h2>Hardware self-tests</h2>"
        "<a class='link-button' href='/debug/screen'><button type='button'>Flash screen (red/green/blue/white)</button></a>"
        "<a class='link-button' href='/debug/speaker'><button type='button'>Play speaker tone</button></a>"
        "<a class='link-button' href='/debug/mic'><button type='button'>Measure microphone level</button></a>"
        "<p class='status'>No gyroscope/IMU on this board - the Waveshare "
        "ESP32-S3-Touch-AMOLED-1.32 doesn't include one (some other Waveshare "
        "AMOLED variants do).</p>"
        "<p><a href='/'>&larr; Back to settings</a></p></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t debug_screen_handler(httpd_req_t *req)
{
    diagnostics_test_screen();
    send_debug_page_head(req, "Debug - Screen");
    httpd_resp_sendstr_chunk(req,
        "<p>The screen should have flashed red, green, blue, then white, "
        "and settled back to black.</p>"
        "<p><a href='/debug'>&larr; Back to debug tools</a></p></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t debug_speaker_handler(httpd_req_t *req)
{
    diagnostics_test_speaker();
    send_debug_page_head(req, "Debug - Speaker");
    httpd_resp_sendstr_chunk(req,
        "<p>Played a short tone - you should have heard a brief beep.</p>"
        "<p><a href='/debug'>&larr; Back to debug tools</a></p></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t debug_mic_handler(httpd_req_t *req)
{
    int16_t peak = diagnostics_test_microphone();
    send_debug_page_head(req, "Debug - Microphone");

    char line[200];
    if (peak < 0) {
        snprintf(line, sizeof(line), "<p class='status bad'>Could not read from the microphone.</p>");
    } else {
        snprintf(line, sizeof(line), "<p>Peak level: <b>%d</b> / 32767</p>", peak);
    }
    httpd_resp_sendstr_chunk(req, line);
    httpd_resp_sendstr_chunk(req,
        "<p class='status'>That was a snapshot of the last half second. Run it again "
        "while talking or clapping close to the board - the number should rise "
        "noticeably above whatever it reads in a quiet room.</p>"
        "<p><a href='/debug/mic'>Test again</a> &middot; "
        "<a href='/debug'>&larr; Back to debug tools</a></p></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

void settings_server_start(void)
{
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 12;
    /* See docs/HARDWARE_REFERENCE.md - networking/provisioning hit a stack
     * overflow on the default 4KB the moment a client connected for real. */
    http_cfg.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &http_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start settings HTTP server");
        return;
    }

    httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = root_get_handler },
        { .uri = "/save",          .method = HTTP_POST, .handler = save_post_handler },
        { .uri = "/test",          .method = HTTP_GET,  .handler = test_get_handler },
        { .uri = "/scan",          .method = HTTP_GET,  .handler = scan_get_handler },
        { .uri = "/debug",         .method = HTTP_GET,  .handler = debug_get_handler },
        { .uri = "/debug/screen",  .method = HTTP_GET,  .handler = debug_screen_handler },
        { .uri = "/debug/speaker", .method = HTTP_GET,  .handler = debug_speaker_handler },
        { .uri = "/debug/mic",     .method = HTTP_GET,  .handler = debug_mic_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "settings server started");
}
