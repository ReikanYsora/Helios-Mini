#include "provisioning.h"
#include "board_config.h"
#include "storage.h"
#include "helios_config.h"
#include "display.h"
#include "qr.h"
#include "figtree.h"
#include "http_forms.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "dhcpserver/dhcpserver.h"
#include "driver/gpio.h"
#include "lvgl.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "provisioning";
static const char *NVS_NAMESPACE = HELIOS_NVS_NAMESPACE;

#define PROVISIONING_AP_IP        "192.168.4.1"
#define PROVISIONING_MAX_FORM_LEN 512
#define PROVISIONING_MAX_SCAN_RESULTS 20

/* Live STA connect for the unified setup flow: the connect POST joins the
 * chosen network while the AP stays up and only persists the credentials
 * once it actually connects. */
static EventGroupHandle_t s_sta_events;
#define STA_CONNECTED_BIT BIT0
#define STA_FAILED_BIT    BIT1
static int s_sta_retry;
static volatile bool s_connecting;

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

#define SETUP_STEPS   4
#define SETUP_ACCENT  lv_color_hex(0xefb428)
#define SETUP_PENDING lv_color_hex(0x46464f)

/* Outer progress ring: SETUP_STEPS segments near the screen edge that light
 * up amber as onboarding advances (current_step is 0-based). On-device
 * feedback alongside the phone wizard. */
static void draw_step_ring(lv_obj_t *parent, int current_step)
{
    const int diam = 440;
    const int gap_deg = 12;
    const int seg_deg = 360 / SETUP_STEPS;
    /* Each segment's midpoint is on a diagonal; the numbered badge sits there. */
    static const lv_point_t badge[SETUP_STEPS] = {
        {384, 82}, {384, 384}, {82, 384}, {82, 82}
    };
    for (int i = 0; i < SETUP_STEPS; i++) {
        bool on = (i <= current_step);
        lv_color_t col = on ? SETUP_ACCENT : SETUP_PENDING;

        lv_obj_t *arc = lv_arc_create(parent);
        lv_obj_set_size(arc, diam, diam);
        lv_obj_center(arc);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_arc_set_bg_angles(arc, (270 + i * seg_deg + gap_deg / 2) % 360,
                                  (270 + (i + 1) * seg_deg - gap_deg / 2) % 360);
        lv_obj_set_style_arc_color(arc, col, LV_PART_MAIN);

        lv_obj_t *b = lv_obj_create(parent);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 40, 40);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, col, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_pos(b, badge[i].x - 20, badge[i].y - 20);
        lv_obj_t *num = lv_label_create(b);
        lv_label_set_text_fmt(num, "%d", i + 1);
        lv_obj_set_style_text_font(num, &figtree_24, 0);
        lv_obj_set_style_text_color(num, on ? lv_color_black() : lv_color_hex(0x9a9aa5), 0);
        lv_obj_center(num);
    }
}

static void show_setup_screen(const char *ap_ssid)
{
    if (!display_lock(1000)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    draw_step_ring(scr, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Scan to set up");
    lv_obj_set_style_text_font(title, &figtree_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -122);

    char wifi_qr[80];
    snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:S:%s;T:nopass;;", ap_ssid);
    lv_obj_t *qr = helios_qr_create(scr, wifi_qr, 160);
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 2);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text_fmt(hint, "or join %s\nand open http://%s/", ap_ssid, PROVISIONING_AP_IP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9a9aa5), 0);
    lv_obj_set_width(hint, 240);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 135);

    display_unlock();
}

/* Setup screen for the in-progress steps: the numbered ring advanced to
 * `step` (0-based) plus a status line, in place of the step-1 QR. */
static void show_setup_progress(int step, const char *text)
{
    if (!display_lock(1000)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    draw_step_ring(scr, step);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &figtree_24, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, 280);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_center(label);

    display_unlock();
}

static void sta_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && s_connecting) {
        if (s_sta_retry < 3) {
            s_sta_retry++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_sta_events, STA_FAILED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_sta_events, STA_CONNECTED_BIT);
    }
}

/* Joins the chosen network on the STA side while the AP stays up, blocking
 * until it connects or gives up (~20s). Called on the httpd task, so the
 * phone's request just waits for the result. */
static bool try_connect_sta(const char *ssid, const char *password)
{
    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid) - 1);
    strncpy((char *)sta.sta.password, password, sizeof(sta.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &sta);

    s_sta_retry = 0;
    s_connecting = true;
    xEventGroupClearBits(s_sta_events, STA_CONNECTED_BIT | STA_FAILED_BIT);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_sta_events, STA_CONNECTED_BIT | STA_FAILED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    s_connecting = false;
    if (bits & STA_CONNECTED_BIT) {
        return true;
    }
    esp_wifi_disconnect(); /* stop the background retry on failure/timeout */
    return false;
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
    (void)arg;
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

    char escaped_ssid[200];
    http_form_html_escape(final_ssid, escaped_ssid, sizeof(escaped_ssid));

    /* Join the network live, AP still up for the phone. Nothing is persisted
     * unless it actually connects - a failed attempt leaves the device
     * unconfigured, so setup restarts from scratch next boot. */
    show_setup_progress(1, "Connecting to Wi-Fi...");
    bool connected = try_connect_sta(final_ssid, password);

    if (!connected) {
        show_setup_progress(1, "Wi-Fi failed");
        char resp[800];
        snprintf(resp, sizeof(resp),
            "<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em;"
            "background:#0b0b0f;color:#eee;text-align:center}h1{color:#e07a5f}a{color:#efb428}</style>"
            "</head><body><h1>Couldn't connect</h1>"
            "<p>Helios Mini couldn't join <b>%s</b>. Check the password and try again.</p>"
            "<p><a href='/'>&larr; Back</a></p></body></html>", escaped_ssid);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, resp);
        return ESP_OK;
    }

    storage_set_string(NVS_NAMESPACE, HELIOS_NVS_KEY_WIFI_SSID, final_ssid);
    storage_set_string(NVS_NAMESPACE, HELIOS_NVS_KEY_WIFI_PASS, password);
    ESP_LOGI(TAG, "connected to '%s', credentials saved", final_ssid);
    show_setup_progress(2, "Wi-Fi connected");

    char resp[800];
    snprintf(resp, sizeof(resp),
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em;"
        "background:#0b0b0f;color:#eee;text-align:center}h1{color:#7cd992}</style>"
        "</head><body><h1>Connected!</h1>"
        "<p>Helios Mini joined <b>%s</b> and is restarting.</p>"
        "<p>When its screen shows a code, scan it with your Camera to link Home Assistant.</p>"
        "<p>You can close this page.</p></body></html>", escaped_ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, resp);

    /* Reboot into normal mode: wifi is saved, so the next boot joins the home
     * network (the AP is gone) and shows its address. Home Assistant is linked
     * from there in a real browser - the captive window can't do the
     * leave-and-return a token needs. */
    const esp_timer_create_args_t timer_args = { .callback = &reboot_timer_cb, .name = "wifi_done_reboot" };
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_once(timer, 2500 * 1000);
    return ESP_OK;
}

/* Captive-portal DNS: answer every query with the AP's own IP, so a phone
 * that joins the setup network gets pointed here for its connectivity check
 * - which pops the setup page automatically. */
static void dns_server_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGW(TAG, "captive DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t pkt[256];
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);
    for (;;) {
        int n = recvfrom(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&client, &clen);
        if (n < 12 || (pkt[2] & 0x80)) { /* too short, or already a response */
            continue;
        }
        pkt[2] = 0x81; /* QR=1, AA=1 */
        pkt[3] = 0x80; /* RA=1, RCODE=0 */
        pkt[6] = 0x00;
        pkt[7] = 0x01; /* one answer */

        int qend = 12;
        while (qend < n && pkt[qend] != 0) {
            qend += pkt[qend] + 1;
        }
        qend += 1 + 4; /* terminating 0 label + QTYPE + QCLASS */
        if (qend > n || qend + 16 > (int)sizeof(pkt)) {
            continue;
        }
        const uint8_t ans[16] = {
            0xC0, 0x0C,             /* name -> the question at offset 12 */
            0x00, 0x01,             /* type A */
            0x00, 0x01,             /* class IN */
            0x00, 0x00, 0x00, 0x3C, /* TTL 60s */
            0x00, 0x04,             /* rdlength 4 */
            192, 168, 4, 1          /* -> 192.168.4.1 */
        };
        memcpy(pkt + qend, ans, sizeof(ans));
        sendto(sock, pkt, qend + 16, 0, (struct sockaddr *)&client, clen);
    }
}

/* Any URL that isn't one of ours (the OS connectivity-check probes) gets a
 * 200 HTML page - not a 302, which iOS won't auto-open on - and that pops
 * the captive sheet; it meta-refreshes straight to the setup page. */
static esp_err_t captive_portal_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    static const char page[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta http-equiv='refresh' content='0;url=http://" PROVISIONING_AP_IP "/'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Helios Mini</title></head>"
        "<body style='background:#0b0b0f;color:#eee;font-family:sans-serif;text-align:center;margin-top:3em'>"
        "<h1 style='color:#efb428'>Helios Mini</h1>"
        "<p><a href='http://" PROVISIONING_AP_IP "/' style='color:#efb428'>Tap to set up</a></p>"
        "</body></html>";
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page);
    return ESP_OK;
}

void provisioning_start_portal(void)
{
    char ap_ssid[24];
    build_ap_ssid(ap_ssid, sizeof(ap_ssid));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta(); /* APSTA mode, so scanning works while the AP serves clients */

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    s_sta_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event_handler, NULL));

    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN; /* first-boot setup network; see docs/HARDWARE_REFERENCE.md */
    ap_config.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    /* Advertise ourselves as the DNS server over DHCP *before* the AP comes
     * up, so the very first client lease already points its connectivity
     * check at us - otherwise the first join gets a lease without DNS and
     * the captive page only pops on a second try. */
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_str_to_ip4(PROVISIONING_AP_IP, &dns.ip.u_addr.ip4);
    esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    dhcps_offer_t offer_dns = OFFER_DNS;
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));

    xTaskCreate(dns_server_task, "captive_dns", 3072, NULL, 5, NULL);

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
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_handler);
}
