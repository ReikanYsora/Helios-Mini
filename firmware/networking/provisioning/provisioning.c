#include "provisioning.h"
#include "board_config.h"
#include "storage.h"
#include "helios_config.h"
#include "display.h"
#include "http_forms.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "lvgl.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "provisioning";
static const char *NVS_NAMESPACE = HELIOS_NVS_NAMESPACE;

#define PROVISIONING_AP_IP        "192.168.4.1"
#define PROVISIONING_MAX_FORM_LEN 512
#define PROVISIONING_REBOOT_DELAY_US (1500 * 1000)
#define PROVISIONING_MAX_SCAN_RESULTS 20

bool provisioning_has_credentials(void)
{
    char ssid[33] = {0};
    return storage_get_string(NVS_NAMESPACE, HELIOS_NVS_KEY_WIFI_SSID, ssid, sizeof(ssid)) == ESP_OK && ssid[0] != '\0';
}

bool provisioning_boot_forced(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << HELIOS_PIN_BUTTON_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    return gpio_get_level(HELIOS_PIN_BUTTON_BOOT) == 0;
}

static void build_ap_ssid(char *out, size_t out_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_size, "HELIOS-MINI-%02X%02X", mac[4], mac[5]);
}

static void show_setup_screen(const char *ap_ssid)
{
    if (!display_lock(1000)) {
        return;
    }
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text_fmt(label,
        "Wi-Fi setup\n\nConnect to:\n%s\n\nThen open:\nhttp://%s/",
        ap_ssid, PROVISIONING_AP_IP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    display_unlock();
}

/* ---- HTTP handlers ---- */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    /* wifi_ap_record_t is large enough (~1.9KB for 20 of them) that keeping
     * it on the httpd task's stack overflowed the default 4KB stack the
     * moment a client actually connected - see docs/HARDWARE_REFERENCE.md.
     * Heap-allocated instead, on top of also bumping stack_size below. */
    wifi_ap_record_t *aps = calloc(PROVISIONING_MAX_SCAN_RESULTS, sizeof(wifi_ap_record_t));
    if (aps == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    uint16_t ap_count = PROVISIONING_MAX_SCAN_RESULTS;
    wifi_scan_config_t scan_cfg = {0};

    /* Blocking active scan on the STA side of APSTA mode; the AP keeps
     * serving concurrently. Takes roughly 1-2s. */
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK ||
        esp_wifi_scan_get_ap_records(&ap_count, aps) != ESP_OK) {
        ap_count = 0;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Helios Mini setup</title>"
        "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em;"
        "background:#0b0b0f;color:#eee}h1{color:#efb428}"
        "select,input,button{width:100%;padding:.6em;margin:.4em 0;box-sizing:border-box;"
        "border-radius:6px;border:1px solid #444;background:#1a1a22;color:#eee;font-size:1em}"
        "button{background:#efb428;color:#111;font-weight:bold;border:none}"
        "label{font-size:.9em;opacity:.8}</style></head><body>"
        "<h1>Helios Mini</h1><p>Choose your Wi-Fi network.</p>"
        "<form method='POST' action='/connect'>"
        "<label>Network</label><select name='ssid'>");

    char escaped[200];
    char chunk[512];
    for (int i = 0; i < ap_count; i++) {
        http_form_html_escape((const char *)aps[i].ssid, escaped, sizeof(escaped));
        snprintf(chunk, sizeof(chunk), "<option value='%s'>%s (%d dBm)</option>",
                 escaped, escaped, aps[i].rssi);
        httpd_resp_sendstr_chunk(req, chunk);
    }

    httpd_resp_sendstr_chunk(req,
        "</select>"
        "<label>Or type a network name manually</label>"
        "<input type='text' name='ssid_manual' placeholder='Network name (optional)'>"
        "<label>Password</label>"
        "<input type='password' name='password'>"
        "<button type='submit'>Connect</button>"
        "</form></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    free(aps);
    return ESP_OK;
}

static void reboot_timer_cb(void *arg)
{
    esp_restart();
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > PROVISIONING_MAX_FORM_LEN) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form too large");
        return ESP_FAIL;
    }

    char body[PROVISIONING_MAX_FORM_LEN + 1] = {0};
    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read failed");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[33] = {0};
    char ssid_manual[33] = {0};
    char password[65] = {0};
    http_form_get(body, "ssid", ssid, sizeof(ssid));
    http_form_get(body, "ssid_manual", ssid_manual, sizeof(ssid_manual));
    http_form_get(body, "password", password, sizeof(password));

    const char *final_ssid = ssid_manual[0] != '\0' ? ssid_manual : ssid;
    if (final_ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no network selected");
        return ESP_FAIL;
    }

    storage_set_string(NVS_NAMESPACE, HELIOS_NVS_KEY_WIFI_SSID, final_ssid);
    storage_set_string(NVS_NAMESPACE, HELIOS_NVS_KEY_WIFI_PASS, password);
    ESP_LOGI(TAG, "credentials saved for '%s', rebooting into station mode", final_ssid);

    char escaped_ssid[200];
    http_form_html_escape(final_ssid, escaped_ssid, sizeof(escaped_ssid));
    char resp[640];
    snprintf(resp, sizeof(resp),
        "<!doctype html><html><body style='font-family:sans-serif;text-align:center;margin-top:3em;"
        "background:#0b0b0f;color:#eee'>"
        "<h1 style='color:#efb428'>Connecting&hellip;</h1>"
        "<p>Helios Mini is restarting and will try to join <b>%s</b>.</p>"
        "<p>You can close this page.</p></body></html>", escaped_ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, resp);

    const esp_timer_create_args_t timer_args = {
        .callback = &reboot_timer_cb,
        .name = "provisioning_reboot",
    };
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_once(timer, PROVISIONING_REBOOT_DELAY_US);

    return ESP_OK;
}

void provisioning_start_portal(void)
{
    char ap_ssid[24];
    build_ap_ssid(ap_ssid, sizeof(ap_ssid));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta(); /* APSTA mode, so scanning works while the AP serves clients */

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN; /* first-boot setup network; see docs/HARDWARE_REFERENCE.md */
    ap_config.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "provisioning AP '%s' started, http://%s/ once connected", ap_ssid, PROVISIONING_AP_IP);
    show_setup_screen(ap_ssid);

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 4;
    /* Default 4KB overflowed in practice once a client actually connected
     * (see docs/HARDWARE_REFERENCE.md) - the handlers' own buffers plus the
     * server's internal request/header parsing need more room. */
    http_cfg.stack_size = 8192;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &http_cfg));

    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t connect_uri = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler };
    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &connect_uri);
}
