#include "settings_server.h"
#include "storage.h"
#include "helios_config.h"
#include "http_forms.h"
#include "ha_client.h"
#include "ha_discovery.h"
#include "diagnostics.h"
#include "wifi_sta.h"
#include "mdi_icons.h"
#include "energy_config.h"
#include "energy_model.h"
#include "ha_ws.h"
#include "mqtt_bridge.h"
#include "display.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "settings_server";

#define SETTINGS_MAX_FORM_LEN     512
#define SETTINGS_HA_URL_LEN       128
#define SETTINGS_HA_TOKEN_LEN     256
#define SETTINGS_MAX_SCAN_RESULTS 8
#define ICON_ON_ACCENT "#111" /* icon color for icons drawn on the amber accent button */

/* ---- persisted HA config/status - unchanged from before this UI pass ---- */

static ha_client_status_t get_stored_status(void)
{
    char key[16] = {0};
    storage_get_string(HELIOS_NVS_NAMESPACE, HELIOS_NVS_KEY_HA_STATUS, key, sizeof(key));
    return ha_client_status_from_key(key);
}

static void store_status(ha_client_status_t status)
{
    storage_set_string(HELIOS_NVS_NAMESPACE, HELIOS_NVS_KEY_HA_STATUS, ha_client_status_key(status));
}

/* ---- page shell: topbar (logo + connection status) + sidebar nav ---- */

static const char *PAGE_STYLE =
    ":root{--bg:#0b0b0f;--card:#15151c;--border:#26262f;--text:#eee;--text-dim:#9a9aa5;"
    "--accent:#efb428;--ok:#7cd992;--bad:#e07a5f;--warn:#e0b45f}"
    "*{box-sizing:border-box}"
    "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
    "background:var(--bg);color:var(--text)}"
    ".topbar{display:flex;align-items:center;justify-content:space-between;padding:.7em 1em;"
    "border-bottom:1px solid var(--border)}"
    ".brand{display:flex;align-items:center;gap:.5em;font-weight:600;font-size:1.05em}"
    ".status-icons{display:flex;gap:1em}"
    ".status-item{display:flex;align-items:center}"
    ".layout{display:flex;align-items:stretch;min-height:100vh}"
    ".sidebar{width:170px;flex-shrink:0;border-right:1px solid var(--border);padding:.8em 0}"
    ".sidebar a{display:flex;align-items:center;gap:.7em;padding:.65em 1em;color:var(--text-dim);"
    "text-decoration:none;font-size:.92em;border-left:3px solid transparent}"
    ".sidebar a.active{color:var(--text);border-left-color:var(--accent);"
    "background:rgba(239,180,40,.08)}"
    ".main{flex:1;padding:1.3em;max-width:480px}"
    "h1{font-size:1.25em;margin:0 0 .7em}"
    "h2{font-size:.85em;color:var(--accent);margin:1.6em 0 .5em;text-transform:uppercase;"
    "letter-spacing:.05em}"
    ".card{background:var(--card);border:1px solid var(--border);border-radius:10px;"
    "padding:.2em 1em;margin-bottom:1em}"
    ".row{display:flex;justify-content:space-between;align-items:center;padding:.55em 0;"
    "font-size:.9em;gap:1em;border-bottom:1px solid var(--border)}"
    ".row:last-child{border-bottom:none}"
    ".row .label{color:var(--text-dim);min-width:0;overflow-wrap:anywhere}"
    "input,button{width:100%;padding:.65em;margin:.4em 0;box-sizing:border-box;"
    "border-radius:8px;border:1px solid var(--border);background:#1a1a22;color:var(--text);"
    "font-size:1em}"
    "button{background:var(--accent);color:#111;font-weight:600;border:none;cursor:pointer;"
    "display:flex;align-items:center;justify-content:center;gap:.5em}"
    "button.secondary{background:#1a1a22;color:var(--text);border:1px solid var(--border)}"
    "button.danger{background:var(--bad);color:#111}"
    "a.link-button{display:block;text-decoration:none}"
    "a.link-button button{pointer-events:none}"
    "label{font-size:.85em;color:var(--text-dim);display:block;margin-top:.7em}"
    ".pill{display:inline-flex;align-items:center;gap:.35em;padding:.3em .7em;"
    "border-radius:20px;font-size:.8em;font-weight:600;flex-shrink:0;white-space:nowrap}"
    ".pill.ok{background:rgba(124,217,146,.15);color:var(--ok)}"
    ".pill.bad{background:rgba(224,122,95,.15);color:var(--bad)}"
    ".pill.warn{background:rgba(224,180,95,.15);color:var(--warn)}"
    ".hint{font-size:.82em;color:var(--text-dim);line-height:1.5;margin:.6em 0}"
    "a{color:var(--accent)}"
    /* Toggle switch (checkbox styled as a track+thumb) - no JS, just the
     * :checked sibling selector. Overrides the generic label{} rule above
     * (block, margin-top) since this label wraps the input directly. */
    ".toggle{position:relative;display:inline-block;width:44px;height:24px;flex-shrink:0;margin:0}"
    ".toggle input{opacity:0;width:0;height:0;position:absolute}"
    ".toggle .track{position:absolute;inset:0;background:#1a1a22;border:1px solid var(--border);"
    "border-radius:999px;transition:.15s;cursor:pointer}"
    ".toggle .track:before{content:'';position:absolute;height:16px;width:16px;left:3px;top:3px;"
    "background:var(--text-dim);border-radius:50%;transition:.15s}"
    ".toggle input:checked+.track{background:var(--accent);border-color:var(--accent)}"
    ".toggle input:checked+.track:before{transform:translateX(20px);background:#111}"
    "@media (max-width:640px){.sidebar{width:58px}.sidebar a span{display:none}"
    ".sidebar a{justify-content:center;padding:.9em 0}.main{padding:1em}}";

/* httpd_resp_sendstr_chunk(req, s) with s=="" sends a zero-length chunk -
 * which, per esp_http_server's own chunked-encoding contract, is exactly
 * the signal that ends the response early (same call shape as the
 * deliberate NULL terminator in close_page()). An empty dynamic value (an
 * unconfigured entity id, an unset URL field, ...) would silently truncate
 * the whole page right there. Skipping empty strings here is a no-op for
 * the HTML either way, so every dynamic sendstr_chunk() in this file goes
 * through this instead - see docs/HARDWARE_REFERENCE.md. */
static void send_chunk(httpd_req_t *req, const char *s)
{
    if (s != NULL && s[0] != '\0') {
        httpd_resp_sendstr_chunk(req, s);
    }
}

/* Emits <svg viewBox='0 0 24 24' .../> for an MDI path (see mdi_icons.h).
 * Sent as separate chunks so the (long, variable-length) path data never
 * has to fit through a bounded snprintf buffer - see docs/HARDWARE_REFERENCE.md
 * on the format-truncation bugs that came from exactly that pattern earlier. */
static void send_icon(httpd_req_t *req, const char *path_d, const char *color, int size)
{
    char open_tag[128];
    snprintf(open_tag, sizeof(open_tag),
             "<svg viewBox='0 0 24 24' width='%d' height='%d' style='fill:%s;flex-shrink:0'><path d='",
             size, size, color);
    send_chunk(req, open_tag);
    send_chunk(req, path_d);
    send_chunk(req, "'/></svg>");
}

static void send_logo(httpd_req_t *req, int size)
{
    char open_tag[128];
    snprintf(open_tag, sizeof(open_tag),
             "<svg viewBox='0 0 512 512' width='%d' height='%d' style='fill:#efb428;flex-shrink:0'><path d='",
             size, size);
    send_chunk(req, open_tag);
    send_chunk(req, HELIOS_LOGO_PATH);
    send_chunk(req, "'/></svg>");
}

/* A '.row' with a label and a real left/right toggle switch (see the CSS
 * ".toggle" rules) instead of a bare checkbox - every on/off setting in
 * this app goes through this. */
static void send_toggle_row(httpd_req_t *req, const char *label, const char *name, bool checked)
{
    send_chunk(req, "<div class='row'><span class='label'>");
    send_chunk(req, label);
    send_chunk(req, "</span><label class='toggle'><input type='checkbox' name='");
    send_chunk(req, name);
    send_chunk(req, "' value='1'");
    if (checked) {
        send_chunk(req, " checked");
    }
    send_chunk(req, "><span class='track'></span></label></div>");
}

static void send_topbar(httpd_req_t *req)
{
    diagnostics_status_t status;
    diagnostics_get_status(&status);
    ha_client_status_t ha_status = get_stored_status();

    send_chunk(req, "<div class='topbar'><div class='brand'>");
    send_logo(req, 26);
    send_chunk(req, "<span>Helios Mini</span></div><div class='status-icons'>");

    send_chunk(req, "<span class='status-item' title='Wi-Fi'>");
    send_icon(req, status.wifi_connected ? ICON_WIFI : ICON_WIFI_OFF,
              status.wifi_connected ? "var(--ok)" : "var(--bad)", 19);
    send_chunk(req, "</span>");

    const char *ha_color = (ha_status == HA_CLIENT_STATUS_OK) ? "var(--ok)"
                          : (ha_status == HA_CLIENT_STATUS_UNKNOWN) ? "var(--text-dim)"
                          : "var(--bad)";
    send_chunk(req, "<span class='status-item' title='Home Assistant'>");
    send_icon(req, ICON_HOME_ASSISTANT, ha_color, 19);
    send_chunk(req, "</span>");

    mqtt_bridge_state_t mqtt_state = mqtt_bridge_get_state();
    const char *mqtt_color = (mqtt_state == MQTT_BRIDGE_CONNECTED) ? "var(--ok)"
                            : (mqtt_state == MQTT_BRIDGE_DISABLED) ? "var(--text-dim)"
                            : "var(--bad)";
    send_chunk(req, "<span class='status-item' title='MQTT'>");
    send_icon(req, ICON_SERVER_NETWORK, mqtt_color, 19);
    send_chunk(req, "</span></div></div>");
}

static void send_nav_link(httpd_req_t *req, const char *href, const char *icon_path,
                           const char *label, const char *active, const char *key)
{
    bool is_active = strcmp(active, key) == 0;
    send_chunk(req, is_active ? "<a class='active' href='" : "<a href='");
    send_chunk(req, href);
    send_chunk(req, "'>");
    send_icon(req, icon_path, "currentColor", 19);
    send_chunk(req, "<span>");
    send_chunk(req, label);
    send_chunk(req, "</span></a>");
}

static void send_sidebar(httpd_req_t *req, const char *active)
{
    send_chunk(req, "<div class='sidebar'>");
    send_nav_link(req, "/network", ICON_LAN, "Network", active, "network");
    send_nav_link(req, "/ha", ICON_HOME_ASSISTANT, "Home Assistant", active, "ha");
    send_nav_link(req, "/display", ICON_CHART_DONUT, "Display", active, "display");
    send_nav_link(req, "/mqtt", ICON_SERVER_NETWORK, "MQTT", active, "mqtt");
    send_nav_link(req, "/debug", ICON_BUG, "Debug", active, "debug");
    send_chunk(req, "</div>");
}

static void open_page(httpd_req_t *req, const char *title, const char *active)
{
    httpd_resp_set_type(req, "text/html");
    send_chunk(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'><title>");
    send_chunk(req, title);
    send_chunk(req, "</title><style>");
    send_chunk(req, PAGE_STYLE);
    send_chunk(req, "</style></head><body>");
    send_topbar(req);
    send_chunk(req, "<div class='layout'>");
    send_sidebar(req, active);
    send_chunk(req, "<div class='main'>");
}

static void close_page(httpd_req_t *req)
{
    send_chunk(req, "</div></div></body></html>");
    /* httpd_resp_send_chunk() with a zero-length buffer is the documented
     * way to terminate a chunked response - must go through the raw call,
     * not send_chunk() (which skips empty strings on purpose, see above). */
    httpd_resp_sendstr_chunk(req, NULL);
}

/* ---- / : redirect to the default page ---- */

static esp_err_t root_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/network");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---- /network : Wi-Fi status + setup-AP toggle ---- */

static esp_err_t network_get_handler(httpd_req_t *req)
{
    diagnostics_status_t status;
    diagnostics_get_status(&status);

    open_page(req, "Helios Mini - Network", "network");
    send_chunk(req, "<h1>Network</h1><div class='card'>");

    send_chunk(req, "<div class='row'><span class='label'>Wi-Fi</span><span>");
    send_chunk(req, status.wifi_connected
        ? "<span class='pill ok'>Connected</span>" : "<span class='pill bad'>Disconnected</span>");
    send_chunk(req, "</span></div>");

    if (status.wifi_connected) {
        char row[128];
        snprintf(row, sizeof(row),
                 "<div class='row'><span class='label'>Signal</span><span>%d dBm</span></div>",
                 status.wifi_rssi);
        send_chunk(req, row);
    }
    send_chunk(req, "</div>");

    send_chunk(req, "<h2>Setup network</h2><div class='card' style='padding:1em'>");

    bool ap_on = wifi_sta_is_setup_ap_enabled();
    char ap_ssid[24];
    wifi_sta_get_setup_ap_ssid(ap_ssid, sizeof(ap_ssid));

    if (ap_on) {
        char msg[300];
        snprintf(msg, sizeof(msg),
                 "<p class='hint'>Active - join <b>%s</b> from another phone or laptop and "
                 "browse to <b>http://192.168.4.1/</b> to reach this same app.</p>", ap_ssid);
        send_chunk(req, msg);
        send_chunk(req, "<a class='link-button' href='/network/disable-ap'><button type='button'>");
        send_icon(req, ICON_ACCESS_POINT_OFF, ICON_ON_ACCENT, 18);
        send_chunk(req, "<span>Turn off setup network</span></button></a>");
    } else {
        send_chunk(req,
            "<p class='hint'>Off. Turn it on to let another phone or laptop join this device "
            "directly and reach this settings app, without disturbing the current Wi-Fi "
            "connection.</p>");
        send_chunk(req, "<a class='link-button' href='/network/enable-ap'><button type='button'>");
        send_icon(req, ICON_ACCESS_POINT, ICON_ON_ACCENT, 18);
        send_chunk(req, "<span>Turn on setup network</span></button></a>");
    }

    send_chunk(req,
        "<p class='hint'>To change which Wi-Fi network this device connects to, hold the "
        "BOOT button while powering it on - that reopens full setup.</p></div>");

    close_page(req);
    return ESP_OK;
}

static esp_err_t network_enable_ap_handler(httpd_req_t *req)
{
    wifi_sta_set_setup_ap_enabled(true);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/network");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t network_disable_ap_handler(httpd_req_t *req)
{
    wifi_sta_set_setup_ap_enabled(false);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/network");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---- /ha : Home Assistant URL + token, scan, test ---- */

static esp_err_t ha_get_handler(httpd_req_t *req)
{
    char url[SETTINGS_HA_URL_LEN] = {0};
    char token[SETTINGS_HA_TOKEN_LEN] = {0};
    bool configured = helios_config_get_ha_credentials(url, sizeof(url), token, sizeof(token));

    /* A /ha/scan pick arrives as "/ha?ha_url=..." - prefill without saving
     * yet, the user still has to press Save (no silent auto-connect). */
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
                              : (status == HA_CLIENT_STATUS_UNKNOWN) ? "warn" : "bad";

    open_page(req, "Helios Mini - Home Assistant", "ha");
    send_chunk(req, "<h1>Home Assistant</h1>");

    char status_line[220];
    snprintf(status_line, sizeof(status_line), "<p class='pill %s'>%s</p>",
             status_class, configured ? ha_client_status_text(status) : "Not configured yet");
    send_chunk(req, status_line);

    ha_ws_status_t ws;
    ha_ws_get_status(&ws);
    if (ws.instance_info_loaded) {
        send_chunk(req, "<h2>Instance found</h2><div class='card'>");

        send_chunk(req, "<div class='row'><span class='label'>Name</span><span>");
        send_chunk(req, ws.instance_name[0] != '\0' ? ws.instance_name : "\xe2\x80\x94");
        send_chunk(req, "</span></div>");

        send_chunk(req, "<div class='row'><span class='label'>Address</span><span>");
        send_chunk(req, escaped_url);
        send_chunk(req, "</span></div>");

        const char *state_class = (strcmp(ws.instance_state, "RUNNING") == 0) ? "ok" : "warn";
        send_chunk(req, "<div class='row'><span class='label'>State</span><span class='pill ");
        send_chunk(req, state_class);
        send_chunk(req, "'>");
        send_chunk(req, ws.instance_state[0] != '\0' ? ws.instance_state : "Unknown");
        send_chunk(req, "</span></div></div>");
    }

    send_chunk(req, "<a class='link-button' href='/ha/scan'><button type='button'>");
    send_icon(req, ICON_MAGNIFY, ICON_ON_ACCENT, 18);
    send_chunk(req, "<span>Scan for Home Assistant</span></button></a>");

    send_chunk(req,
        "<form method='POST' action='/ha/save'>"
        "<label>Home Assistant URL</label>"
        "<input type='text' name='ha_url' placeholder='http://192.168.0.2:8123' value='");
    send_chunk(req, escaped_url);
    send_chunk(req,
        "'>"
        "<label>Long-Lived Access Token</label>"
        "<input type='password' name='ha_token' placeholder='paste your token here'>"
        "<button type='submit'>");
    send_icon(req, ICON_CONTENT_SAVE, ICON_ON_ACCENT, 18);
    send_chunk(req, "<span>Save &amp; test</span></button></form>");

    if (configured) {
        send_chunk(req, "<a class='link-button' href='/ha/test'><button type='button' class='secondary'>");
        send_icon(req, ICON_REFRESH, "var(--text)", 18);
        send_chunk(req, "<span>Test connection again</span></button></a>");
    }

    send_chunk(req,
        "<p class='hint'>Generate a token in Home Assistant: profile (bottom left) "
        "&rarr; Security &rarr; Long-Lived Access Tokens. Tokens don't expire on a "
        "schedule, but Home Assistant rejects them once revoked - if the status above "
        "says the token was rejected, generate a fresh one and paste it here again.</p>");

    close_page(req);
    return ESP_OK;
}

static void hard_reset_timer_cb(void *arg); /* defined below - a clean reboot */

static esp_err_t ha_save_post_handler(httpd_req_t *req)
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
        storage_set_string(HELIOS_NVS_NAMESPACE, HELIOS_NVS_KEY_HA_URL, url);
    }
    if (token[0] != '\0') {
        storage_set_string(HELIOS_NVS_NAMESPACE, HELIOS_NVS_KEY_HA_TOKEN, token);
    }

    /* Re-read what's actually stored (a save with only one field filled in
     * keeps the other one from before) and test it right away - "feedback,
     * not silence" per how this project wants things built. */
    char stored_url[SETTINGS_HA_URL_LEN] = {0};
    char stored_token[SETTINGS_HA_TOKEN_LEN] = {0};
    helios_config_get_ha_credentials(stored_url, sizeof(stored_url), stored_token, sizeof(stored_token));
    ha_client_status_t status = ha_client_test_connection(stored_url, stored_token);
    store_status(status);

    ESP_LOGI(TAG, "settings saved (url set: %s, token set: %s), test result: %s",
             url[0] ? "yes" : "no", token[0] ? "yes" : "no", ha_client_status_key(status));

    if (status == HA_CLIENT_STATUS_OK) {
        /* Restart to apply: a clean boot connects the Home Assistant
         * WebSocket reliably and then shows the rings; a live reconnect
         * right after setup is flaky (it comes up before the network is
         * ready and doesn't always recover). */
        open_page(req, "Helios Mini - Saved", "ha");
        send_chunk(req, "<h1>Home Assistant linked</h1>"
            "<p>Helios Mini is restarting to show your energy. You can close this page.</p>");
        close_page(req);

        const esp_timer_create_args_t reboot_args = { .callback = &hard_reset_timer_cb, .name = "ha_saved_reboot" };
        esp_timer_handle_t reboot_timer;
        esp_timer_create(&reboot_args, &reboot_timer);
        esp_timer_start_once(reboot_timer, 1500 * 1000);
        return ESP_OK;
    }

    open_page(req, "Helios Mini - Saved", "ha");
    send_chunk(req, "<h1>Saved</h1><p>");
    send_chunk(req, ha_client_status_text(status));
    send_chunk(req, "</p><p><a href='/ha'>&larr; Back</a></p>");
    close_page(req);
    return ESP_OK;
}

static esp_err_t ha_test_get_handler(httpd_req_t *req)
{
    char url[SETTINGS_HA_URL_LEN] = {0};
    char token[SETTINGS_HA_TOKEN_LEN] = {0};
    helios_config_get_ha_credentials(url, sizeof(url), token, sizeof(token));
    store_status(ha_client_test_connection(url, token));

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/ha");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t ha_scan_get_handler(httpd_req_t *req)
{
    /* wifi_ap_record_t-sized lesson learned earlier applies here too -
     * heap-allocate, don't put a multi-entry results array on the stack. */
    ha_discovery_result_t *results = calloc(SETTINGS_MAX_SCAN_RESULTS, sizeof(ha_discovery_result_t));
    if (results == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int count = ha_discovery_scan(results, SETTINGS_MAX_SCAN_RESULTS, 3000);

    open_page(req, "Helios Mini - Scanning", "ha");
    send_chunk(req, "<h1>Home Assistant</h1><p class='hint'>Found on your network:</p>");

    if (count == 0) {
        send_chunk(req,
            "<p class='hint'>No Home Assistant instance answered mDNS on this network. "
            "It may have discovery/Zeroconf disabled - enter the URL manually instead.</p>");
    }

    char escaped_name[100];
    char escaped_url[SETTINGS_HA_URL_LEN + 8];
    char encoded_url[SETTINGS_HA_URL_LEN * 3];
    for (int i = 0; i < count; i++) {
        http_form_html_escape(results[i].name, escaped_name, sizeof(escaped_name));
        http_form_html_escape(results[i].url, escaped_url, sizeof(escaped_url));
        http_form_url_encode(results[i].url, encoded_url, sizeof(encoded_url));

        send_chunk(req, "<a class='link-button' href='/ha?ha_url=");
        send_chunk(req, encoded_url);
        send_chunk(req,
            "'><button type='button' class='secondary' style='justify-content:flex-start;text-align:left'>");
        send_icon(req, ICON_HOME_ASSISTANT, "var(--accent)", 22);
        send_chunk(req, "<span>");
        send_chunk(req, escaped_name);
        send_chunk(req, "<br><small style='opacity:.7'>");
        send_chunk(req, escaped_url);
        send_chunk(req, "</small></span></button></a>");
    }

    send_chunk(req, "<p><a href='/ha'>&larr; Back</a></p>");
    close_page(req);
    free(results);
    return ESP_OK;
}

/* ---- /display : auto-discovered Energy Dashboard sources + ring limits ----
 *
 * Zero manual entity entry - helios/ha_ws reads Home Assistant's own
 * Energy Dashboard configuration (Settings -> Dashboards -> Energy) and
 * subscribes to live updates for whatever it finds. "gere TOUS LES CAS":
 * every source row is one of exactly four states - not set up in Home
 * Assistant's Energy Dashboard / Home Assistant not set up on Helios Mini
 * at all / waiting for a first live update / a live value (flagged stale
 * if it hasn't updated in a while). Nothing is ever silently blank. */

static void format_age(int64_t age_ms, char *out, size_t out_len)
{
    int64_t s = age_ms / 1000;
    if (s < 60) {
        snprintf(out, out_len, "%llds ago", (long long)s);
    } else if (s < 3600) {
        snprintf(out, out_len, "%lldm ago", (long long)(s / 60));
    } else {
        snprintf(out, out_len, "%lldh ago", (long long)(s / 3600));
    }
}

static void render_source_row(httpd_req_t *req, const char *label, const ha_ws_source_t *src, bool ha_ready,
                               const energy_format_t *fmt)
{
    send_chunk(req, "<div class='row'><span class='label'>");
    send_chunk(req, label);
    if (src->entity_id[0] != '\0') {
        char escaped[HA_WS_ENTITY_LIST_LEN + 8];
        http_form_html_escape(src->entity_id, escaped, sizeof(escaped));
        send_chunk(req, "<br><small style='opacity:.6'>");
        send_chunk(req, escaped);
        send_chunk(req, "</small>");
    }
    send_chunk(req, "</span><span class='pill ");

    if (!ha_ready) {
        send_chunk(req, "warn'>");
        send_icon(req, ICON_ALERT_CIRCLE, "var(--warn)", 15);
        send_chunk(req, "<span>Home Assistant not set up</span></span></div>");
        return;
    }
    if (src->status == HA_WS_SOURCE_NOT_CONFIGURED) {
        send_chunk(req, "warn'>");
        send_icon(req, ICON_CIRCLE_OFF_OUTLINE, "var(--text-dim)", 15);
        send_chunk(req, "<span>Not set up in your Energy Dashboard</span></span></div>");
        return;
    }
    if (src->status == HA_WS_SOURCE_WAITING) {
        send_chunk(req, "warn'>");
        send_icon(req, ICON_ALERT_CIRCLE, "var(--warn)", 15);
        send_chunk(req, "<span>Waiting for first update</span></span></div>");
        return;
    }

    char value_text[24];
    energy_format_power(src->power_w, fmt, value_text, sizeof(value_text));

    if (src->status == HA_WS_SOURCE_STALE) {
        char age_text[24];
        format_age(src->age_ms, age_text, sizeof(age_text));
        send_chunk(req, "warn'>");
        send_icon(req, ICON_ALERT_CIRCLE, "var(--warn)", 15);
        send_chunk(req, "<span>");
        send_chunk(req, value_text);
        send_chunk(req, " (Stale, ");
        send_chunk(req, age_text);
        send_chunk(req, ")</span></span></div>");
        return;
    }

    send_chunk(req, "ok'>");
    send_icon(req, ICON_CHECK_CIRCLE, "var(--ok)", 15);
    send_chunk(req, "<span>");
    send_chunk(req, value_text);
    send_chunk(req, "</span></span></div>");
}

static esp_err_t display_get_handler(httpd_req_t *req)
{
    energy_limits_t limits;
    energy_config_load_limits(&limits);
    energy_format_t fmt;
    energy_config_load_format(&fmt);

    char ha_url[SETTINGS_HA_URL_LEN] = {0};
    char ha_token[SETTINGS_HA_TOKEN_LEN] = {0};
    bool ha_ready = helios_config_get_ha_credentials(ha_url, sizeof(ha_url), ha_token, sizeof(ha_token));

    ha_ws_status_t ws;
    ha_ws_get_status(&ws);

    open_page(req, "Helios Mini - Display", "display");
    send_chunk(req, "<h1>Display</h1>");

    if (!ha_ready) {
        send_chunk(req,
            "<p class='hint'>Home Assistant isn't set up yet. <a href='/ha'>Configure it "
            "first</a>, then come back here.</p>");
        close_page(req);
        return ESP_OK;
    }

    const char *link_class = (ws.link == HA_WS_LINK_CONNECTED) ? "ok"
                            : (ws.link == HA_WS_LINK_AUTH_FAILED) ? "bad" : "warn";
    const char *link_text = (ws.link == HA_WS_LINK_CONNECTED) ? "Connected"
                           : (ws.link == HA_WS_LINK_AUTH_FAILED)
                               ? "Token rejected, fix it on the Home Assistant page"
                           : (ws.link == HA_WS_LINK_CONNECTING) ? "Connecting\xe2\x80\xa6"
                           : "Disconnected, retrying";
    char link_line[300];
    snprintf(link_line, sizeof(link_line), "<p class='pill %s'>%s</p>", link_class, link_text);
    send_chunk(req, link_line);

    if (ws.link == HA_WS_LINK_CONNECTED && ws.prefs_loaded && !ws.energy_dashboard_configured) {
        send_chunk(req,
            "<p class='hint'>Your Home Assistant instance doesn't have the Energy Dashboard "
            "configured yet.</p>"
            "<p class='hint'>In Home Assistant, go to <b>Settings &rarr; Dashboards &rarr; "
            "Energy</b>, add your solar/grid/battery entities there, then hit Rescan "
            "below.</p>"
            "<p class='hint'>Helios Mini reads its ring sources straight from that "
            "configuration. Nothing to type here.</p>");
    }

    send_chunk(req, "<h2>Energy Dashboard sources</h2><div class='card'>");
    render_source_row(req, "Solar production", &ws.solar, ha_ready, &fmt);
    render_source_row(req, "Grid import", &ws.grid_import, ha_ready, &fmt);
    render_source_row(req, "Grid export", &ws.grid_export, ha_ready, &fmt);
    render_source_row(req, "Battery charge", &ws.battery_charge, ha_ready, &fmt);
    render_source_row(req, "Battery discharge", &ws.battery_discharge, ha_ready, &fmt);
    send_chunk(req, "</div>");

    send_chunk(req, "<a class='link-button' href='/display/rescan'><button type='button' class='secondary'>");
    send_icon(req, ICON_REFRESH, "var(--text)", 18);
    send_chunk(req, "<span>Rescan Energy Dashboard</span></button></a>");

    send_chunk(req,
        "<p class='hint'>Sources feeding the rings come straight from your Home Assistant "
        "Energy Dashboard configuration. Nothing to type here.</p>"
        "<p class='hint'>Only the live power (W) entity linked to each source is used, the "
        "same one the Helios card itself reads. It updates the instant Home Assistant pushes "
        "a new reading.</p>"
        "<p class='hint'>A source is flagged \xe2\x80\x9cStale\xe2\x80\x9d if it hasn't sent "
        "one in a while. It shows \xe2\x80\x9cNot set up in your Energy Dashboard\xe2\x80\x9d "
        "if that source has no power entity linked there at all (its cumulative energy/kWh "
        "statistic, if any, is never used).</p>"
        "<p class='hint'>The grid and battery rings each show whichever direction is "
        "currently active. For example, the grid ring fills blue while importing, then "
        "switches to purple once exporting.</p>");

    energy_page_visibility_t pages;
    energy_config_load_pages(&pages);

    send_chunk(req, "<h2>Pages</h2><form method='POST' action='/display/save'>");
    send_chunk(req,
        "<p class='hint'>The general view (four rings) always shows. Turn off any dedicated "
        "page you don't need - e.g. no battery installed - to shorten the swipe.</p>");

    send_toggle_row(req, "Irradiance", "page_irradiance", pages.show_irradiance);
    send_toggle_row(req, "Production (solar)", "page_solar", pages.show_solar);
    send_toggle_row(req, "Import / Export", "page_grid", pages.show_grid);
    send_toggle_row(req, "Battery", "page_battery", pages.show_battery);
    send_toggle_row(req, "Home consumption", "page_consumption", pages.show_consumption);

    send_chunk(req,
        "<p class='hint'>Irradiance doesn't need an entity - it's computed from your home's "
        "location in Home Assistant (Settings &rarr; System &rarr; General) and live cloud "
        "cover from Open-Meteo. Shows \xe2\x80\x9cwaiting\xe2\x80\x9d until both resolve, which "
        "can take a few minutes after boot.</p>");

    send_chunk(req, "<h2>Ring limits (100% reference)</h2>");

    char num[32];

    snprintf(num, sizeof(num), "%.0f", limits.max_solar_w);
    send_chunk(req, "<label>Solar ring max (W)</label>"
        "<input type='number' step='1' min='1' name='lim_solar' value='");
    send_chunk(req, num);
    send_chunk(req, "'>");

    snprintf(num, sizeof(num), "%.0f", limits.max_grid_import_w);
    send_chunk(req, "<label>Grid import ring max (W)</label>"
        "<input type='number' step='1' min='1' name='lim_gimport' value='");
    send_chunk(req, num);
    send_chunk(req, "'>");

    snprintf(num, sizeof(num), "%.0f", limits.max_grid_export_w);
    send_chunk(req, "<label>Grid export ring max (W)</label>"
        "<input type='number' step='1' min='1' name='lim_gexport' value='");
    send_chunk(req, num);
    send_chunk(req, "'>");

    snprintf(num, sizeof(num), "%.0f", limits.max_battery_w);
    send_chunk(req, "<label>Battery ring max (W), shared by charge and discharge</label>"
        "<input type='number' step='1' min='1' name='lim_battery' value='");
    send_chunk(req, num);
    send_chunk(req, "'>");

    send_chunk(req, "<h2>Number format</h2>");
    /* Unit is the only choice - precision follows from it on the device
     * (whole watts, or kW to one decimal), so there's no decimals control. */
    send_toggle_row(req, "Show power in kilowatts (kW)", "unit_kw", fmt.use_kw);

    send_chunk(req, "<button type='submit'>");
    send_icon(req, ICON_CONTENT_SAVE, ICON_ON_ACCENT, 18);
    send_chunk(req, "<span>Save</span></button></form>");

    close_page(req);
    return ESP_OK;
}

static esp_err_t display_save_post_handler(httpd_req_t *req)
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

    energy_limits_t limits;
    energy_config_load_limits(&limits); /* start from current/defaults, only overwrite what parses */
    char num[32];
    char *end;
    float parsed;

    http_form_get(body, "lim_solar", num, sizeof(num));
    if (num[0] != '\0') {
        parsed = strtof(num, &end);
        if (end != num && parsed > 0.0f) {
            limits.max_solar_w = parsed;
        }
    }
    http_form_get(body, "lim_gimport", num, sizeof(num));
    if (num[0] != '\0') {
        parsed = strtof(num, &end);
        if (end != num && parsed > 0.0f) {
            limits.max_grid_import_w = parsed;
        }
    }
    http_form_get(body, "lim_gexport", num, sizeof(num));
    if (num[0] != '\0') {
        parsed = strtof(num, &end);
        if (end != num && parsed > 0.0f) {
            limits.max_grid_export_w = parsed;
        }
    }
    http_form_get(body, "lim_battery", num, sizeof(num));
    if (num[0] != '\0') {
        parsed = strtof(num, &end);
        if (end != num && parsed > 0.0f) {
            limits.max_battery_w = parsed;
        }
    }
    energy_config_save_limits(&limits);

    /* Unit toggle: unchecked checkboxes aren't sent, so presence == kW. */
    energy_format_t fmt;
    char unit_kw[4] = {0};
    http_form_get(body, "unit_kw", unit_kw, sizeof(unit_kw));
    fmt.use_kw = unit_kw[0] != '\0';
    energy_config_save_format(&fmt);

    /* Unchecked checkboxes aren't sent at all, so presence in the body -
     * not its value - is what "on" means here. */
    energy_page_visibility_t pages;
    char flag[4];
    http_form_get(body, "page_solar", flag, sizeof(flag));
    pages.show_solar = flag[0] != '\0';
    http_form_get(body, "page_irradiance", flag, sizeof(flag));
    pages.show_irradiance = flag[0] != '\0';
    http_form_get(body, "page_grid", flag, sizeof(flag));
    pages.show_grid = flag[0] != '\0';
    http_form_get(body, "page_battery", flag, sizeof(flag));
    pages.show_battery = flag[0] != '\0';
    http_form_get(body, "page_consumption", flag, sizeof(flag));
    pages.show_consumption = flag[0] != '\0';
    energy_config_save_pages(&pages);

    energy_model_refresh(); /* wake the render loop now, don't wait up to 3s */

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/display");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t display_rescan_handler(httpd_req_t *req)
{
    ha_ws_rescan();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/display");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---- /mqtt : broker config for helios/mqtt_bridge ---- */

static esp_err_t mqtt_get_handler(httpd_req_t *req)
{
    mqtt_config_t cfg;
    mqtt_config_load(&cfg);
    mqtt_bridge_state_t state = mqtt_bridge_get_state();
    const char *state_class = (state == MQTT_BRIDGE_CONNECTED) ? "ok"
                             : (state == MQTT_BRIDGE_DISABLED) ? "warn" : "bad";

    open_page(req, "Helios Mini - MQTT", "mqtt");
    send_chunk(req, "<h1>MQTT</h1>");

    char status_line[64];
    snprintf(status_line, sizeof(status_line), "<p class='pill %s'>", state_class);
    send_chunk(req, status_line);
    send_chunk(req, mqtt_bridge_state_text(state));
    send_chunk(req, "</p>");

    send_chunk(req,
        "<p class='hint'>Exposes on-device controls and diagnostics to Home Assistant as MQTT "
        "entities, auto-discovered the moment this connects (MQTT discovery is on by default in "
        "HA) - screen brightness, Wi-Fi signal, free heap/PSRAM, uptime, and ground irradiance / "
        "cloud cover.</p>");

    send_chunk(req, "<form method='POST' action='/mqtt/save'>");

    send_toggle_row(req, "Enabled", "mqtt_enabled", cfg.enabled);

    char escaped[196];
    http_form_html_escape(cfg.device_name, escaped, sizeof(escaped));
    send_chunk(req, "<label>Device name (shown in Home Assistant)</label>"
        "<input type='text' name='mqtt_dname' placeholder='");
    send_chunk(req, MQTT_DEFAULT_DEVICE_NAME);
    send_chunk(req, "' value='");
    send_chunk(req, escaped);
    send_chunk(req, "'>");

    http_form_html_escape(cfg.host, escaped, sizeof(escaped));
    send_chunk(req, "<label>Broker host</label>"
        "<input type='text' name='mqtt_host' placeholder='192.168.0.2' value='");
    send_chunk(req, escaped);
    send_chunk(req, "'>");

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", cfg.port > 0 ? cfg.port : MQTT_DEFAULT_PORT);
    send_chunk(req, "<label>Broker port</label>"
        "<input type='number' name='mqtt_port' min='1' max='65535' value='");
    send_chunk(req, port_str);
    send_chunk(req, "'>");

    http_form_html_escape(cfg.username, escaped, sizeof(escaped));
    send_chunk(req, "<label>Username (leave empty if the broker allows anonymous connections)</label>"
        "<input type='text' name='mqtt_user' value='");
    send_chunk(req, escaped);
    send_chunk(req, "'>");

    send_chunk(req, "<label>Password</label>"
        "<input type='password' name='mqtt_pass' placeholder='");
    send_chunk(req, cfg.password[0] != '\0' ? "leave blank to keep the current password" : "");
    send_chunk(req, "'>");

    send_chunk(req, "<button type='submit'>");
    send_icon(req, ICON_CONTENT_SAVE, ICON_ON_ACCENT, 18);
    send_chunk(req, "<span>Save &amp; reconnect</span></button></form>");

    send_chunk(req,
        "<p class='hint'>Most Home Assistant installs run Mosquitto on the same host as HA "
        "itself, port 1883 - that's the usual value for \"Broker host\".</p>");

    close_page(req);
    return ESP_OK;
}

static esp_err_t mqtt_save_post_handler(httpd_req_t *req)
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

    mqtt_config_t cfg;
    mqtt_config_load(&cfg); /* start from current - an empty password field means "keep it", not "clear it" */

    char flag[4];
    http_form_get(body, "mqtt_enabled", flag, sizeof(flag));
    cfg.enabled = flag[0] != '\0';

    http_form_get(body, "mqtt_dname", cfg.device_name, sizeof(cfg.device_name));

    http_form_get(body, "mqtt_host", cfg.host, sizeof(cfg.host));

    char port_str[8] = {0};
    http_form_get(body, "mqtt_port", port_str, sizeof(port_str));
    if (port_str[0] != '\0') {
        int parsed = atoi(port_str);
        if (parsed > 0 && parsed <= 65535) {
            cfg.port = parsed;
        }
    }

    http_form_get(body, "mqtt_user", cfg.username, sizeof(cfg.username));

    char password[64] = {0};
    http_form_get(body, "mqtt_pass", password, sizeof(password));
    if (password[0] != '\0') {
        strncpy(cfg.password, password, sizeof(cfg.password) - 1);
    }

    mqtt_config_save(&cfg);
    mqtt_bridge_restart(); /* take effect now, not on next reboot */

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/mqtt");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---- /debug : live diagnostics (values + graphs, no page reload) ---- */

/* Client-side renderer: polls /debug/status.json every few seconds and
 * repaints the value spans and the SVG sparklines in place. Built with the
 * SVG DOM API (no attribute-quote gymnastics inside this C string) and only
 * single quotes / HTML entities (no backslashes) so it embeds cleanly. */
static const char DEBUG_LIVE_SCRIPT[] =
"<script>(function(){"
"var $=function(id){return document.getElementById(id);};"
"function fmtUp(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);"
"if(d>0)return d+'d '+h+'h';if(h>0)return h+'h '+m+'m';return m+'m '+(s%60)+'s';}"
"function el(t,a){var e=document.createElementNS('http://www.w3.org/2000/svg',t);"
"for(var k in a)e.setAttribute(k,a[k]);return e;}"
"function spark(svg,series,lo,hi,markers){var W=300,H=90,all=[];"
"series.forEach(function(s){all=all.concat(s.data);});"
"all=all.filter(function(v){return v!=null&&!isNaN(v);});"
"while(svg.firstChild)svg.removeChild(svg.firstChild);"
"if(all.length<2)return;var mn=lo,mx=hi;"
"if(mn==null){mn=Math.min.apply(null,all);mx=Math.max.apply(null,all);var pad=(mx-mn)*0.15||1;mn-=pad;mx+=pad;}"
"var y=function(v){return H-(Math.max(mn,Math.min(mx,v))-mn)/(mx-mn)*H;};"
"svg.setAttribute('viewBox','0 0 '+W+' '+H);svg.setAttribute('preserveAspectRatio','none');"
"(markers||[]).forEach(function(m){var ym=y(m);"
"svg.appendChild(el('line',{x1:0,y1:ym,x2:W,y2:ym,stroke:'var(--text-dim)','stroke-width':0.6,'stroke-dasharray':'3 3',opacity:0.45}));});"
"series.forEach(function(s){var d=s.data,p=[];for(var i=0;i<d.length;i++){"
"if(d[i]==null||isNaN(d[i]))continue;p.push((i*W/(d.length-1)).toFixed(1)+','+y(d[i]).toFixed(1));}"
"svg.appendChild(el('polyline',{fill:'none',stroke:s.color,'stroke-width':1.6,'stroke-linejoin':'round',points:p.join(' ')}));});}"
"function tick(){fetch('/debug/status.json').then(function(r){return r.json();}).then(function(j){"
"var c=j.cur;$('v-up').textContent=fmtUp(j.uptime);"
"$('v-temp').innerHTML=c.temp.toFixed(1)+' &deg;C';"
"$('v-temp').style.color=c.temp>=80?'var(--bad)':c.temp>=70?'var(--warn)':'var(--ok)';"
"$('v-cpu0').textContent=Math.round(c.cpu0)+' %';"
"$('v-cpu1').textContent=Math.round(c.cpu1)+' %';"
"$('v-heap').textContent=c.heap.toLocaleString()+' B';"
"$('v-psram').textContent=c.psram.toLocaleString()+' B';"
"$('v-rssi').textContent=j.wifi?c.rssi+' dBm':'-';"
"var h=j.hist;"
"spark($('g-temp'),[{data:h.temp,color:'var(--accent)'}],30,85,[70,80]);"
"spark($('g-cpu'),[{data:h.cpu0,color:'var(--accent)'},{data:h.cpu1,color:'var(--ok)'}],0,100);"
"spark($('g-heap'),[{data:h.heap.map(function(x){return x/1024;}),color:'var(--accent)'}],null,null);"
"spark($('g-psram'),[{data:h.psram.map(function(x){return x/1024;}),color:'var(--accent)'}],null,null);"
"spark($('g-rssi'),[{data:h.rssi,color:'var(--accent)'}],-90,-30);"
"}).catch(function(){});}"
"tick();setInterval(tick,3000);})();</script>";

static esp_err_t debug_status_json_handler(httpd_req_t *req)
{
    diagnostics_status_t st;
    diagnostics_get_status(&st);
    diagnostics_sample_t h[DIAGNOSTICS_HISTORY];
    size_t n = 0;
    diagnostics_get_history(h, DIAGNOSTICS_HISTORY, &n);

    httpd_resp_set_type(req, "application/json");
    char b[360];
    snprintf(b, sizeof(b),
        "{\"uptime\":%lld,\"wifi\":%s,\"cur\":{\"temp\":%.1f,\"cpu0\":%.0f,\"cpu1\":%.0f,"
        "\"heap\":%u,\"minheap\":%u,\"psram\":%u,\"rssi\":%d},\"hist\":{",
        (long long)st.uptime_s, st.wifi_connected ? "true" : "false",
        st.temperature_c, st.cpu0_pct, st.cpu1_pct,
        (unsigned)st.free_heap, (unsigned)st.min_free_heap, (unsigned)st.free_psram, st.wifi_rssi);
    httpd_resp_sendstr_chunk(req, b);

    char arr[768];
    size_t len;
#define EMIT_ARR(NAME, FMT, EXPR)                                            \
    len = snprintf(arr, sizeof(arr), "\"" NAME "\":[");                       \
    for (size_t i = 0; i < n && len < sizeof(arr) - 24; i++) {                \
        len += snprintf(arr + len, sizeof(arr) - len, "%s" FMT, i ? "," : "", EXPR); \
    }                                                                        \
    snprintf(arr + len, sizeof(arr) - len, "]");                             \
    httpd_resp_sendstr_chunk(req, arr)

    EMIT_ARR("temp", "%.1f", h[i].temperature_c);
    httpd_resp_sendstr_chunk(req, ",");
    EMIT_ARR("cpu0", "%.0f", h[i].cpu0_pct);
    httpd_resp_sendstr_chunk(req, ",");
    EMIT_ARR("cpu1", "%.0f", h[i].cpu1_pct);
    httpd_resp_sendstr_chunk(req, ",");
    EMIT_ARR("heap", "%u", (unsigned)h[i].free_heap);
    httpd_resp_sendstr_chunk(req, ",");
    EMIT_ARR("psram", "%u", (unsigned)h[i].free_psram);
    httpd_resp_sendstr_chunk(req, ",");
    EMIT_ARR("rssi", "%d", h[i].wifi_rssi);
#undef EMIT_ARR

    httpd_resp_sendstr_chunk(req, "}}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* ---- /debug : hardware self-tests ---- */

static esp_err_t debug_get_handler(httpd_req_t *req)
{
    diagnostics_status_t status;
    diagnostics_get_status(&status);

    open_page(req, "Helios Mini - Debug", "debug");
    send_chunk(req, "<h1>Debug</h1><div class='card'>");

    char row[220];
    snprintf(row, sizeof(row), "<div class='row'><span class='label'>Uptime</span><span id='v-up'>%llds</span></div>",
             (long long)status.uptime_s);
    send_chunk(req, row);
    snprintf(row, sizeof(row), "<div class='row'><span class='label'>Die temperature</span><span id='v-temp'>%.1f &deg;C</span></div>",
             status.temperature_c);
    send_chunk(req, row);
    snprintf(row, sizeof(row), "<div class='row'><span class='label'>CPU core 0</span><span id='v-cpu0'>%.0f %%</span></div>",
             status.cpu0_pct);
    send_chunk(req, row);
    snprintf(row, sizeof(row), "<div class='row'><span class='label'>CPU core 1</span><span id='v-cpu1'>%.0f %%</span></div>",
             status.cpu1_pct);
    send_chunk(req, row);
    snprintf(row, sizeof(row), "<div class='row'><span class='label'>Free internal RAM</span><span id='v-heap'>%u B</span></div>",
             (unsigned)status.free_heap);
    send_chunk(req, row);
    snprintf(row, sizeof(row), "<div class='row'><span class='label'>Free PSRAM</span><span id='v-psram'>%u B</span></div>",
             (unsigned)status.free_psram);
    send_chunk(req, row);
    snprintf(row, sizeof(row), "<div class='row'><span class='label'>Wi-Fi signal</span><span id='v-rssi'>%d dBm</span></div>",
             status.wifi_rssi);
    send_chunk(req, row);
    send_chunk(req, "</div>");

    /* Graphs, filled and refreshed live by DEBUG_LIVE_SCRIPT (polls
     * /debug/status.json, no page reload). */
    send_chunk(req,
        "<h2>Graphs</h2><div class='card'>"
        "<div class='glabel'>Die temperature (&deg;C)</div><svg id='g-temp' class='spark'></svg>"
        "<div class='glabel'>CPU load &mdash; core 0 / core 1 (%)</div><svg id='g-cpu' class='spark'></svg>"
        "<div class='glabel'>Free internal RAM (KB)</div><svg id='g-heap' class='spark'></svg>"
        "<div class='glabel'>Free PSRAM (KB)</div><svg id='g-psram' class='spark'></svg>"
        "<div class='glabel'>Wi-Fi signal (dBm)</div><svg id='g-rssi' class='spark'></svg>"
        "</div>"
        "<style>.spark{width:100%;height:78px;display:block;margin:.2em 0 1em;"
        "background:rgba(255,255,255,.02);border-radius:6px}"
        ".glabel{font-size:.78em;color:var(--text-dim);margin:.4em 0 .1em}</style>");
    send_chunk(req, DEBUG_LIVE_SCRIPT);

    send_chunk(req, "<h2>Screenshot</h2>");
    send_chunk(req, "<a class='link-button' href='/debug/screenshot.bmp'><button type='button'>");
    send_icon(req, ICON_MONITOR, "#111", 18);
    send_chunk(req, "<span>Download screenshot</span></button></a>");
    send_chunk(req,
        "<p class='hint'>Grabs whatever's currently on the device's screen as a BMP file.</p>");

    send_chunk(req, "<h2>Factory reset</h2>");
    send_chunk(req, "<a class='link-button' href='/debug/hard-reset'><button type='button' class='danger'>");
    send_icon(req, ICON_REFRESH, "#111", 18);
    send_chunk(req, "<span>Hard reset</span></button></a>");
    send_chunk(req,
        "<p class='hint'>Erases Wi-Fi credentials, Home Assistant setup and every preference "
        "stored on this device, then restarts into the setup Wi-Fi network so you can "
        "reconfigure it from scratch.</p>");

    close_page(req);
    return ESP_OK;
}

/* Serves display_take_screenshot_bmp()'s output as a download rather than
 * an inline image - Content-Disposition: attachment, so tapping the button
 * saves a file instead of opening it in the browser. */
static esp_err_t debug_screenshot_handler(httpd_req_t *req)
{
    uint8_t *bmp = NULL;
    size_t bmp_len = 0;
    if (!display_take_screenshot_bmp(&bmp, &bmp_len)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "screenshot failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"helios-mini.bmp\"");
    esp_err_t err = httpd_resp_send(req, (const char *)bmp, bmp_len);
    free(bmp);
    return err;
}

static void hard_reset_timer_cb(void *arg)
{
    esp_restart();
}

/* Confirmation screen - the /debug page's own button links here, not
 * straight to the POST below, so one accidental tap doesn't wipe the
 * device. Only the form on this page can actually trigger the reset. */
static esp_err_t debug_hard_reset_confirm_handler(httpd_req_t *req)
{
    open_page(req, "Helios Mini - Hard reset", "debug");
    send_chunk(req, "<h1>Hard reset</h1>");
    send_chunk(req,
        "<p class='pill bad'>This erases everything</p>"
        "<p class='hint'>Wi-Fi credentials, Home Assistant URL and token, ring limits, page "
        "layout, MQTT setup - every preference stored on this device is gone. Helios Mini "
        "restarts and emits its own setup Wi-Fi network, exactly like the first time you "
        "unboxed it.</p>"
        "<form method='POST' action='/debug/hard-reset'>"
        "<button type='submit' class='danger'>");
    send_icon(req, ICON_REFRESH, "#111", 18);
    send_chunk(req, "<span>Yes, erase everything and restart</span></button></form>"
        "<p><a href='/debug'>&larr; Cancel, take me back</a></p>");
    close_page(req);
    return ESP_OK;
}

static esp_err_t debug_hard_reset_post_handler(httpd_req_t *req)
{
    esp_err_t err = storage_erase_all(HELIOS_NVS_NAMESPACE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hard reset: storage_erase_all failed: %s", esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "hard reset requested from /debug - erasing NVS and restarting");

    open_page(req, "Helios Mini - Hard reset", "debug");
    send_chunk(req,
        "<h1>Resetting&hellip;</h1>"
        "<p>Helios Mini is erasing its settings and restarting. It will come back up as its "
        "own Wi-Fi setup network in a few seconds.</p>");
    close_page(req);

    const esp_timer_create_args_t timer_args = {
        .callback = &hard_reset_timer_cb,
        .name = "hard_reset_reboot",
    };
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_once(timer, 1500 * 1000); /* let the response above actually reach the browser first */

    return ESP_OK;
}

void settings_server_start(void)
{
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 24; /* headroom for hard-reset + the upcoming MQTT settings routes */
    /* See docs/HARDWARE_REFERENCE.md - networking/provisioning hit a stack
     * overflow on the default 4KB the moment a client connected for real. */
    http_cfg.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &http_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start settings HTTP server");
        return;
    }

    httpd_uri_t routes[] = {
        { .uri = "/",                   .method = HTTP_GET,  .handler = root_redirect_handler },
        { .uri = "/network",            .method = HTTP_GET,  .handler = network_get_handler },
        { .uri = "/network/enable-ap",  .method = HTTP_GET,  .handler = network_enable_ap_handler },
        { .uri = "/network/disable-ap", .method = HTTP_GET,  .handler = network_disable_ap_handler },
        { .uri = "/ha",                 .method = HTTP_GET,  .handler = ha_get_handler },
        { .uri = "/ha/save",            .method = HTTP_POST, .handler = ha_save_post_handler },
        { .uri = "/ha/test",            .method = HTTP_GET,  .handler = ha_test_get_handler },
        { .uri = "/ha/scan",            .method = HTTP_GET,  .handler = ha_scan_get_handler },
        { .uri = "/display",            .method = HTTP_GET,  .handler = display_get_handler },
        { .uri = "/display/save",       .method = HTTP_POST, .handler = display_save_post_handler },
        { .uri = "/display/rescan",     .method = HTTP_GET,  .handler = display_rescan_handler },
        { .uri = "/mqtt",               .method = HTTP_GET,  .handler = mqtt_get_handler },
        { .uri = "/mqtt/save",          .method = HTTP_POST, .handler = mqtt_save_post_handler },
        { .uri = "/debug",              .method = HTTP_GET,  .handler = debug_get_handler },
        { .uri = "/debug/status.json",  .method = HTTP_GET,  .handler = debug_status_json_handler },
        { .uri = "/debug/screenshot.bmp", .method = HTTP_GET, .handler = debug_screenshot_handler },
        { .uri = "/debug/hard-reset",   .method = HTTP_GET,  .handler = debug_hard_reset_confirm_handler },
        { .uri = "/debug/hard-reset",   .method = HTTP_POST, .handler = debug_hard_reset_post_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "settings server started");
}
