#include "mqtt_bridge.h"
#include "storage.h"
#include "helios_config.h"
#include "display.h"
#include "diagnostics.h"
#include "irradiance_model.h"

#include "mqtt_client.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "mqtt_bridge";
static const char *NVS_NAMESPACE = HELIOS_NVS_NAMESPACE;

#define MQTT_TASK_STACK       4096
#define MQTT_TASK_PRIORITY    3
#define MQTT_PUBLISH_INTERVAL_MS (15 * 1000)
#define MQTT_MAX_COMMAND_HANDLERS 6
/* A broker that accepts the TCP connection but keeps dropping it before
 * the handshake settles (wrong port on a service that's listening for
 * something else, auth rejected mid-handshake, ...) sends this into a
 * tight reconnect loop - each attempt republishing every discovery config
 * (QoS 1, so it sits in esp-mqtt's own outbox until acked) faster than a
 * dropped connection ever acks it. Real symptom hit once: free_heap fell
 * from ~20KB to <3KB over a few minutes of a broker that couldn't be
 * reached, taking the settings web server down with it. Give up after a
 * few failures instead of retrying forever - see s_give_up_requested. */
#define MQTT_MAX_CONSECUTIVE_FAILURES 5
#define MQTT_ID_LEN  8   /* "a1b2c3d4" - 4 MAC bytes in hex, more headroom than the AP SSID's 2-byte suffix since this is a permanent identifier, not a glanced-at Wi-Fi name */
#define MQTT_TOPIC_LEN 192 /* real max is ~20 ("helios_mini/xxxxxxxx/status") - sized with headroom past what GCC's format-truncation checker can statically bound */

static SemaphoreHandle_t s_mutex;
static esp_mqtt_client_handle_t s_client;
static mqtt_bridge_state_t s_state = MQTT_BRIDGE_DISABLED;
static bool s_connected = false;
static volatile int s_consecutive_failures = 0;
/* esp_mqtt_client_stop() explicitly cannot be called from the MQTT event
 * handler (deadlocks - stop() joins the client's own task, which is the
 * task currently running the handler). Set here, actually stopped from
 * publish_task's loop instead. */
static volatile bool s_give_up_requested = false;
/* Same deferral, different reason: mqtt_bridge_restart() is called from
 * the /mqtt/save httpd handler, and esp_mqtt_client_init()/start() must
 * never run on that handler's own task - see mqtt_bridge_restart()'s own
 * comment. */
static volatile bool s_restart_requested = false;
static volatile TickType_t s_heap_guard_grace_until = 0; /* set in start_client() - see MQTT_HEAP_GUARD_GRACE_MS */

static char s_id[MQTT_ID_LEN + 1];
static char s_client_id[16 + MQTT_ID_LEN];
static char s_device_name[64] = MQTT_DEFAULT_DEVICE_NAME; /* set from cfg->device_name in start_client(); read by mqtt_bridge_build_device_json() */
static char s_base_topic[MQTT_TOPIC_LEN];
static char s_avail_topic[MQTT_TOPIC_LEN + 16]; /* base_topic + "/status" - deliberately bigger than s_base_topic itself so GCC's format-truncation checker (which assumes s_base_topic could be filled to its full declared size) can prove this always fits */

typedef struct {
    char topic_suffix[40];
    mqtt_bridge_command_cb_t cb;
} command_handler_t;
static command_handler_t s_handlers[MQTT_MAX_COMMAND_HANDLERS];
static int s_handler_count = 0;
static mqtt_bridge_connected_cb_t s_connected_cb = NULL;

void mqtt_bridge_set_connected_cb(mqtt_bridge_connected_cb_t cb)
{
    s_connected_cb = cb;
}

static bool load_bool(const char *key, bool fallback)
{
    char buf[4] = {0};
    if (storage_get_string(NVS_NAMESPACE, key, buf, sizeof(buf)) != ESP_OK || buf[0] == '\0') {
        return fallback;
    }
    return buf[0] == '1';
}

void mqtt_config_load(mqtt_config_t *out)
{
    memset(out, 0, sizeof(*out));
    storage_get_string(NVS_NAMESPACE, "mqtt_host", out->host, sizeof(out->host));
    storage_get_string(NVS_NAMESPACE, "mqtt_user", out->username, sizeof(out->username));
    storage_get_string(NVS_NAMESPACE, "mqtt_pass", out->password, sizeof(out->password));
    storage_get_string(NVS_NAMESPACE, "mqtt_dname", out->device_name, sizeof(out->device_name));

    char port_str[8] = {0};
    out->port = MQTT_DEFAULT_PORT;
    if (storage_get_string(NVS_NAMESPACE, "mqtt_port", port_str, sizeof(port_str)) == ESP_OK && port_str[0] != '\0') {
        int parsed = atoi(port_str);
        if (parsed > 0 && parsed <= 65535) {
            out->port = parsed;
        }
    }
    out->enabled = load_bool("mqtt_enabled", false);
}

void mqtt_config_save(const mqtt_config_t *cfg)
{
    storage_set_string(NVS_NAMESPACE, "mqtt_host", cfg->host);
    storage_set_string(NVS_NAMESPACE, "mqtt_user", cfg->username);
    storage_set_string(NVS_NAMESPACE, "mqtt_pass", cfg->password);
    storage_set_string(NVS_NAMESPACE, "mqtt_dname", cfg->device_name);
    char port_str[16]; /* headroom past "65535" - GCC's format-truncation checker can't bound %d to the real range */
    snprintf(port_str, sizeof(port_str), "%d", cfg->port > 0 ? cfg->port : MQTT_DEFAULT_PORT);
    storage_set_string(NVS_NAMESPACE, "mqtt_port", port_str);
    storage_set_string(NVS_NAMESPACE, "mqtt_enabled", cfg->enabled ? "1" : "0");
}

const char *mqtt_bridge_state_text(mqtt_bridge_state_t state)
{
    switch (state) {
        case MQTT_BRIDGE_CONNECTING: return "Connecting\xe2\x80\xa6";
        case MQTT_BRIDGE_CONNECTED:  return "Connected";
        case MQTT_BRIDGE_ERROR:      return "Connection error - check the broker settings and Save again";
        default:                     return "Not configured";
    }
}

mqtt_bridge_state_t mqtt_bridge_get_state(void)
{
    mqtt_bridge_state_t s = MQTT_BRIDGE_DISABLED;
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s = s_state;
        xSemaphoreGive(s_mutex);
    }
    return s;
}

static void set_state(mqtt_bridge_state_t state)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_state = state;
        xSemaphoreGive(s_mutex);
    }
}

bool mqtt_bridge_get_base_topic(char *out, size_t out_len)
{
    if (!s_connected) {
        return false;
    }
    snprintf(out, out_len, "%s", s_base_topic);
    return true;
}

bool mqtt_bridge_publish(const char *topic_suffix, const char *payload, bool retain)
{
    if (!s_connected || s_client == NULL) {
        return false;
    }
    char topic[512];
    snprintf(topic, sizeof(topic), "%s/%s", s_base_topic, topic_suffix);
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 0, retain ? 1 : 0);
    return msg_id >= 0;
}

bool mqtt_bridge_publish_raw_config(const char *topic, const char *payload)
{
    if (!s_connected || s_client == NULL) {
        return false;
    }
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);
    return msg_id >= 0;
}

void mqtt_bridge_set_command_handler(const char *topic_suffix, mqtt_bridge_command_cb_t cb)
{
    if (s_handler_count >= MQTT_MAX_COMMAND_HANDLERS) {
        ESP_LOGW(TAG, "no room left for command handler '%s'", topic_suffix);
        return;
    }
    strncpy(s_handlers[s_handler_count].topic_suffix, topic_suffix, sizeof(s_handlers[0].topic_suffix) - 1);
    s_handlers[s_handler_count].cb = cb;
    s_handler_count++;
}

cJSON *mqtt_bridge_build_device_json(void)
{
    cJSON *device = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    cJSON_AddItemToArray(ids, cJSON_CreateString(s_client_id));
    cJSON_AddItemToObject(device, "identifiers", ids);
    cJSON_AddStringToObject(device, "name", s_device_name[0] != '\0' ? s_device_name : MQTT_DEFAULT_DEVICE_NAME);
    cJSON_AddStringToObject(device, "manufacturer", "Helios");
    cJSON_AddStringToObject(device, "model", "Helios Mini V0.1");
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc != NULL) {
        cJSON_AddStringToObject(device, "sw_version", app_desc->version);
    }
    return device;
}

/* One HA MQTT discovery publish - component is "number"/"sensor"/
 * "binary_sensor"/"media_player"/...; object_id is this entity's slug
 * within the device (unique_id becomes "<client_id>_<object_id>"). extra
 * is merged into the payload (component-specific fields: unit_of_measurement,
 * min/max, device_class, command_topic, ...) and consumed (freed) here.
 * Every entity shares state_topic="<base>/<object_id>/state" and the same
 * availability_topic/device block, so callers only ever add what's
 * actually different about their entity. */
static void publish_discovery(const char *component, const char *object_id, const char *name, cJSON *extra)
{
    cJSON *root = extra != NULL ? extra : cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    char unique_id[512];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", s_client_id, object_id);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
    if (cJSON_GetObjectItem(root, "state_topic") == NULL) {
        char state_topic[512];
        snprintf(state_topic, sizeof(state_topic), "%s/%s/state", s_base_topic, object_id);
        cJSON_AddStringToObject(root, "state_topic", state_topic);
    }
    cJSON_AddStringToObject(root, "availability_topic", s_avail_topic);
    cJSON_AddItemToObject(root, "device", mqtt_bridge_build_device_json());

    char config_topic[512];
    snprintf(config_topic, sizeof(config_topic), "homeassistant/%s/%s/%s/config", component, s_client_id, object_id);
    char *payload = cJSON_PrintUnformatted(root);
    if (payload != NULL) {
        esp_mqtt_client_publish(s_client, config_topic, payload, 0, 1, 1); /* QoS 1, retained */
        free(payload);
    }
    cJSON_Delete(root);
}

static void publish_number_discovery(const char *object_id, const char *name, int min, int max, const char *unit)
{
    cJSON *extra = cJSON_CreateObject();
    char cmd_topic[MQTT_TOPIC_LEN + 24];
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/%s/set", s_base_topic, object_id);
    cJSON_AddStringToObject(extra, "command_topic", cmd_topic);
    cJSON_AddNumberToObject(extra, "min", min);
    cJSON_AddNumberToObject(extra, "max", max);
    cJSON_AddNumberToObject(extra, "step", 1);
    if (unit != NULL) {
        cJSON_AddStringToObject(extra, "unit_of_measurement", unit);
    }
    publish_discovery("number", object_id, name, extra);
}

static void publish_sensor_discovery(const char *object_id, const char *name, const char *unit,
                                      const char *device_class, bool diagnostic)
{
    cJSON *extra = cJSON_CreateObject();
    if (unit != NULL) {
        cJSON_AddStringToObject(extra, "unit_of_measurement", unit);
    }
    if (device_class != NULL) {
        cJSON_AddStringToObject(extra, "device_class", device_class);
    }
    if (diagnostic) {
        cJSON_AddStringToObject(extra, "entity_category", "diagnostic");
    }
    publish_discovery("sensor", object_id, name, extra);
}

static void publish_binary_sensor_discovery(const char *object_id, const char *name, const char *device_class)
{
    cJSON *extra = cJSON_CreateObject();
    if (device_class != NULL) {
        cJSON_AddStringToObject(extra, "device_class", device_class);
    }
    cJSON_AddStringToObject(extra, "payload_on", "ON");
    cJSON_AddStringToObject(extra, "payload_off", "OFF");
    publish_discovery("binary_sensor", object_id, name, extra);
}

static void publish_all_discovery(void)
{
    publish_number_discovery("brightness", "Brightness", 0, 100, "%");
    publish_sensor_discovery("wifi_rssi", "Wi-Fi signal", "dBm", "signal_strength", true);
    publish_binary_sensor_discovery("wifi_connected", "Wi-Fi connected", "connectivity");
    publish_sensor_discovery("free_heap", "Free heap", "B", NULL, true);
    publish_sensor_discovery("free_psram", "Free PSRAM", "B", NULL, true);
    publish_sensor_discovery("uptime", "Uptime", "s", "duration", true);
    publish_sensor_discovery("die_temperature", "Die temperature", "\xc2\xb0" "C", "temperature", true);
    publish_sensor_discovery("cpu_usage", "CPU usage", "%", NULL, true);
    publish_sensor_discovery("irradiance", "Irradiance", "W/m\xc2\xb2", "irradiance", false);
    publish_sensor_discovery("cloud_cover", "Cloud cover", "%", NULL, false);
}

static void handle_brightness_set(const char *payload, size_t len)
{
    char buf[16] = {0};
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    int pct = atoi(buf);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    display_set_brightness((uint8_t)(pct * 255 / 100));
    char state[8];
    snprintf(state, sizeof(state), "%d", pct);
    mqtt_bridge_publish("brightness/state", state, true);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "connected to broker");
            s_connected = true;
            s_consecutive_failures = 0;
            set_state(MQTT_BRIDGE_CONNECTED);
            esp_mqtt_client_publish(s_client, s_avail_topic, "online", 0, 1, 1);
            publish_all_discovery();
            for (int i = 0; i < s_handler_count; i++) {
                char topic[512]; /* generous past base_topic+suffix's real max - GCC's format-truncation checker can't bound it tighter */
                snprintf(topic, sizeof(topic), "%s/%s", s_base_topic, s_handlers[i].topic_suffix);
                esp_mqtt_client_subscribe(s_client, topic, 1);
            }
            if (s_connected_cb != NULL) {
                s_connected_cb();
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            set_state(MQTT_BRIDGE_ERROR);
            s_consecutive_failures++;
            if (s_consecutive_failures >= MQTT_MAX_CONSECUTIVE_FAILURES) {
                ESP_LOGW(TAG, "disconnected %d times in a row - giving up, fix the broker settings and Save again",
                         s_consecutive_failures);
                s_give_up_requested = true;
            } else {
                ESP_LOGW(TAG, "disconnected - will auto-reconnect");
            }
            break;
        case MQTT_EVENT_DATA: {
            for (int i = 0; i < s_handler_count; i++) {
                char topic[512];
                int topic_len = snprintf(topic, sizeof(topic), "%s/%s", s_base_topic, s_handlers[i].topic_suffix);
                if (topic_len == event->topic_len && strncmp(topic, event->topic, event->topic_len) == 0) {
                    s_handlers[i].cb(event->data, event->data_len);
                    break;
                }
            }
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
            set_state(MQTT_BRIDGE_ERROR);
            s_consecutive_failures++;
            if (s_consecutive_failures >= MQTT_MAX_CONSECUTIVE_FAILURES) {
                s_give_up_requested = true;
            }
            break;
        default:
            break;
    }
}

static void stop_client(void); /* defined below - can't be called from mqtt_event_handler itself, see s_give_up_requested */
static void start_client(const mqtt_config_t *cfg); /* defined below - needed by publish_task for s_restart_requested */

/* Real symptom hit once (see MQTT_MAX_CONSECUTIVE_FAILURES above): a
 * broker that TCP-accepts but never behaves like a working MQTT service
 * can stay in a half-connected state for a long time - "connected" fires,
 * but every subsequent subscribe/publish stalls on its own write timeout
 * (~10s each) without necessarily ever firing a clean
 * MQTT_EVENT_DISCONNECTED in between. The event-count circuit breaker
 * above doesn't reliably catch that shape of failure. This is the direct
 * backstop: whatever the cause, if internal free heap actually falls this
 * low while MQTT is even trying to connect, stop protecting a broker and
 * start protecting the rest of the device (the web server + LVGL + Wi-Fi
 * all share this same pool). Checked every MQTT_HEAP_GUARD_POLL_MS, well
 * under MQTT_PUBLISH_INTERVAL_MS, so it can't itself sit idle for 15s
 * while heap circles the drain.
 *
 * Suppressed for MQTT_HEAP_GUARD_GRACE_MS after every (re)connect attempt
 * starts: publish_all_discovery() fires synchronously right on
 * MQTT_EVENT_CONNECTED and its burst of 9 cJSON-built publishes is a real,
 * expected, self-resolving dip - not a leak.
 *
 * Also requires MQTT_HEAP_GUARD_LOW_STREAK consecutive low polls, not one -
 * caught this live too: even well past the grace window (60-80s into a
 * healthy run, MQTT never having logged an error), a single 5s sample dipped
 * under 12000B purely from this device's normal background churn (HA
 * WebSocket/irradiance polling sharing the same internal-RAM pool) and got
 * torn down for it, even though the very next diagnostics_task reading
 * 30s later was back at ~28KB free. One bad sample is noise on this
 * device; a genuine runaway (bad broker, tight reconnect loop) stays low
 * for multiple consecutive polls in a row and still gets caught. */
#define MQTT_HEAP_GUARD_MIN_BYTES  12000
#define MQTT_HEAP_GUARD_POLL_MS    5000
#define MQTT_HEAP_GUARD_GRACE_MS   20000
#define MQTT_HEAP_GUARD_LOW_STREAK 3 /* 3 * MQTT_HEAP_GUARD_POLL_MS = 15s of sustained low heap before giving up */

static volatile int s_heap_guard_low_streak = 0;

static void give_up(const char *reason)
{
    ESP_LOGW(TAG, "stopping the MQTT client and disabling it: %s (re-enable on /mqtt once the broker settings are fixed)",
              reason);
    stop_client();
    mqtt_config_t cfg;
    mqtt_config_load(&cfg);
    cfg.enabled = false; /* persisted - a bad config must not retry itself back into the same hole on the next boot */
    mqtt_config_save(&cfg);
    set_state(MQTT_BRIDGE_ERROR);
}

static void publish_task(void *arg)
{
    (void)arg;
    int ticks_since_publish = 0;
    const int publish_every_n_ticks = MQTT_PUBLISH_INTERVAL_MS / MQTT_HEAP_GUARD_POLL_MS;

    for (;;) {
        if (s_restart_requested) {
            s_restart_requested = false;
            stop_client();
            mqtt_config_t cfg;
            mqtt_config_load(&cfg);
            if (cfg.enabled && cfg.host[0] != '\0') {
                start_client(&cfg);
            } else {
                set_state(MQTT_BRIDGE_DISABLED);
            }
        }
        if (s_give_up_requested) {
            s_give_up_requested = false;
            give_up("too many consecutive connection failures");
        } else if (s_client == NULL || (int32_t)(xTaskGetTickCount() - s_heap_guard_grace_until) < 0) {
            s_heap_guard_low_streak = 0; /* no client, or still inside the post-connect grace window - nothing to accumulate */
        } else if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MQTT_HEAP_GUARD_MIN_BYTES) {
            /* esp_get_free_heap_size() reports internal+PSRAM combined -
             * with PSRAM's 8MB barely touched that number never drops
             * meaningfully, so it never trips. Internal RAM specifically
             * (MALLOC_CAP_INTERNAL) is the pool the web server/LVGL/Wi-Fi
             * actually compete over, and the one diagnostics.c itself
             * reports as "free_heap" - same measurement, so this threshold
             * means what the numbers in the serial log and /debug page say. */
            s_heap_guard_low_streak++;
            if (s_heap_guard_low_streak >= MQTT_HEAP_GUARD_LOW_STREAK) {
                give_up("free heap stayed too low for too long while connecting/connected");
            }
        } else {
            s_heap_guard_low_streak = 0; /* recovered - back to a clean slate */
        }

        ticks_since_publish++;
        if (s_connected && ticks_since_publish >= publish_every_n_ticks) {
            ticks_since_publish = 0;
            diagnostics_status_t status;
            diagnostics_get_status(&status);

            char buf[24];
            snprintf(buf, sizeof(buf), "%d", status.wifi_rssi);
            mqtt_bridge_publish("wifi_rssi/state", buf, false);
            mqtt_bridge_publish("wifi_connected/state", status.wifi_connected ? "ON" : "OFF", false);
            snprintf(buf, sizeof(buf), "%u", (unsigned)status.free_heap);
            mqtt_bridge_publish("free_heap/state", buf, false);
            snprintf(buf, sizeof(buf), "%u", (unsigned)status.free_psram);
            mqtt_bridge_publish("free_psram/state", buf, false);
            snprintf(buf, sizeof(buf), "%lld", (long long)status.uptime_s);
            mqtt_bridge_publish("uptime/state", buf, false);
            snprintf(buf, sizeof(buf), "%.1f", (double)status.temperature_c);
            mqtt_bridge_publish("die_temperature/state", buf, false);
            snprintf(buf, sizeof(buf), "%.0f", (double)((status.cpu0_pct + status.cpu1_pct) / 2.0f));
            mqtt_bridge_publish("cpu_usage/state", buf, false);

            irradiance_reading_t irr = irradiance_model_get();
            if (irr.status == IRRADIANCE_LIVE) {
                snprintf(buf, sizeof(buf), "%.0f", (double)irr.wm2);
                mqtt_bridge_publish("irradiance/state", buf, false);
                snprintf(buf, sizeof(buf), "%.0f", (double)irr.cloud_cover_pct);
                mqtt_bridge_publish("cloud_cover/state", buf, false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(MQTT_HEAP_GUARD_POLL_MS));
    }
}

static void compute_identity(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_id, sizeof(s_id), "%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_client_id, sizeof(s_client_id), "helios_mini_%s", s_id);
    snprintf(s_base_topic, sizeof(s_base_topic), "helios_mini/%s", s_id);
    snprintf(s_avail_topic, sizeof(s_avail_topic), "%s/status", s_base_topic);
}

static void start_client(const mqtt_config_t *cfg)
{
    compute_identity();
    snprintf(s_device_name, sizeof(s_device_name), "%s",
             cfg->device_name[0] != '\0' ? cfg->device_name : MQTT_DEFAULT_DEVICE_NAME);
    s_consecutive_failures = 0; /* a fresh Save deserves a fresh run at MQTT_MAX_CONSECUTIVE_FAILURES, not one already spent */
    s_give_up_requested = false;
    s_heap_guard_grace_until = xTaskGetTickCount() + pdMS_TO_TICKS(MQTT_HEAP_GUARD_GRACE_MS);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = cfg->host,
        .broker.address.port = (uint32_t)(cfg->port > 0 ? cfg->port : MQTT_DEFAULT_PORT),
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username = cfg->username[0] != '\0' ? cfg->username : NULL,
        .credentials.authentication.password = cfg->password[0] != '\0' ? cfg->password : NULL,
        .credentials.client_id = s_client_id,
        .session.last_will = {
            .topic = s_avail_topic,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
        /* Default reconnect is aggressive (~10s) - fine against a broker
         * that's merely restarting, punishing against one that's simply
         * wrong (bad host/port): each attempt republishes every discovery
         * config at QoS 1, and an outbox with no explicit cap can grow
         * without bound while a flapping connection never lives long
         * enough to ack them - see MQTT_MAX_CONSECUTIVE_FAILURES above,
         * this is the other half of the same fix. */
        .network.reconnect_timeout_ms = 20000,
        .outbox.limit = 8192,
    };
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        set_state(MQTT_BRIDGE_ERROR);
        return;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    /* Registered once, here - the handler table isn't cleared across
     * mqtt_bridge_restart() calls, only the client is torn down/recreated,
     * so brightness stays subscribed across a settings change. */
    if (s_handler_count == 0) {
        mqtt_bridge_set_command_handler("brightness/set", handle_brightness_set);
    }

    set_state(MQTT_BRIDGE_CONNECTING);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        set_state(MQTT_BRIDGE_ERROR);
    }
}

static void stop_client(void)
{
    if (s_client != NULL) {
        s_connected = false;
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}

void mqtt_bridge_start(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        /* The publish/restart/give-up task always exists, even with MQTT
         * disabled - it's the only thing that can ever process a later
         * mqtt_bridge_restart() (the first Save on a device that has
         * never connected before has nothing else to act on that flag),
         * and idle-polling every MQTT_HEAP_GUARD_POLL_MS while disabled
         * costs nothing but its own MQTT_TASK_STACK. */
        xTaskCreate(publish_task, "mqtt_bridge_pub", MQTT_TASK_STACK, NULL, MQTT_TASK_PRIORITY, NULL);
    }

    mqtt_config_t cfg;
    mqtt_config_load(&cfg);
    if (!cfg.enabled || cfg.host[0] == '\0') {
        set_state(MQTT_BRIDGE_DISABLED);
        return;
    }
    start_client(&cfg);
}

void mqtt_bridge_restart(void)
{
    s_restart_requested = true; /* actually acted on by publish_task - see its own comment on why not here */
}
