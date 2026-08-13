#include "ha_ws.h"
#include "ha_client.h"
#include "storage.h"

#include "esp_websocket_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "ha_ws";
/* Same NVS namespace/keys networking/settings_server writes ha_url/ha_token
 * under - duplicated on purpose, same reasoning as helios/energy_model:
 * this component intentionally doesn't depend on settings_server. */
static const char *NVS_NAMESPACE = "helios_mini";

#define HA_WS_URL_LEN         192
#define HA_WS_TOKEN_LEN       256
#define HA_WS_TASK_STACK      8192
#define HA_WS_TASK_PRIORITY   3
#define HA_WS_WS_BUFFER_SIZE  4096
#define HA_WS_WAIT_MS         5000  /* how long ha_ws_task blocks on the rx queue per loop */
#define HA_WS_STALE_AFTER_S   600   /* 10 minutes with no update on every contributor -> stale */

/* Home Assistant's Energy Dashboard lets more than one source feed the
 * same ring - most commonly a dual-tariff grid meter (two separate power
 * sensors is unusual but the schema allows it) or multiple solar
 * strings/battery packs. Up to this many contributors are summed per
 * ring; real-world setups rarely exceed 2-3. */
#define HA_WS_MAX_SUB 4

typedef enum {
    SLOT_SOLAR,
    SLOT_GRID_IMPORT,
    SLOT_GRID_EXPORT,
    SLOT_BATTERY_CHARGE,
    SLOT_BATTERY_DISCHARGE,
    SLOT_COUNT,
} slot_t;

typedef struct {
    char entity_id[HA_WS_ENTITY_ID_LEN];
    int subscribe_id;   /* "id" used for this entity's subscribe_trigger, 0 = not (yet) subscribed */
    int64_t last_sample_us;
    float power_w;
    bool live;           /* has a usable reading (bootstrap or a push update) */
} sub_entity_t;

typedef struct {
    sub_entity_t subs[HA_WS_MAX_SUB];
    int sub_count;
} slot_state_t;

static slot_state_t s_slots[SLOT_COUNT];
static ha_ws_link_state_t s_link = HA_WS_LINK_DISCONNECTED;
static bool s_prefs_loaded = false;
static bool s_energy_dashboard_configured = false;

static SemaphoreHandle_t s_mutex;     /* guards s_slots[]/s_link/... - everything ha_ws_get_status() reads */
static SemaphoreHandle_t s_rescan_sem;
static QueueHandle_t s_rx_queue;      /* holds heap-owned, NUL-terminated char* JSON messages */
static esp_websocket_client_handle_t s_client;

static bool s_authenticated = false;
static int s_next_id = 1;
static int s_prefs_request_id = 0;
static char s_started_url[HA_WS_URL_LEN] = {0};
static char s_started_token[HA_WS_TOKEN_LEN] = {0};

/* Reassembly buffer for websocket text frames delivered across multiple
 * WEBSOCKET_EVENT_DATA callbacks (esp_websocket_client fragments large
 * frames rather than blocking until a full one is buffered). */
static char *s_frag_buf = NULL;
static int s_frag_total = 0;
static int s_frag_filled = 0;

static bool load_ha_credentials(char *url_out, size_t url_len, char *token_out, size_t token_len)
{
    url_out[0] = '\0';
    token_out[0] = '\0';
    bool have_url = storage_get_string(NVS_NAMESPACE, "ha_url", url_out, url_len) == ESP_OK && url_out[0] != '\0';
    bool have_token = storage_get_string(NVS_NAMESPACE, "ha_token", token_out, token_len) == ESP_OK && token_out[0] != '\0';
    return have_url && have_token;
}

static void build_ws_url(const char *http_url, char *out, size_t out_len)
{
    const char *scheme_ws = "ws://";
    const char *rest = http_url;
    if (strncmp(http_url, "https://", 8) == 0) {
        rest = http_url + 8;
        scheme_ws = "wss://";
    } else if (strncmp(http_url, "http://", 7) == 0) {
        rest = http_url + 7;
        scheme_ws = "ws://";
    }
    char trimmed[HA_WS_URL_LEN];
    strncpy(trimmed, rest, sizeof(trimmed) - 1);
    trimmed[sizeof(trimmed) - 1] = '\0';
    size_t len = strlen(trimmed);
    while (len > 0 && trimmed[len - 1] == '/') {
        trimmed[--len] = '\0';
    }
    snprintf(out, out_len, "%s%s/api/websocket", scheme_ws, trimmed);
}

static void send_json(cJSON *obj)
{
    if (s_client == NULL || obj == NULL) {
        cJSON_Delete(obj);
        return;
    }
    char *s = cJSON_PrintUnformatted(obj);
    if (s != NULL) {
        esp_websocket_client_send_text(s_client, s, strlen(s), pdMS_TO_TICKS(5000));
        cJSON_free(s);
    }
    cJSON_Delete(obj);
}

static void send_auth(const char *token)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "auth");
    cJSON_AddStringToObject(obj, "access_token", token);
    send_json(obj);
}

static void request_prefs(void)
{
    int id = s_next_id++;
    s_prefs_request_id = id;
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", id);
    cJSON_AddStringToObject(obj, "type", "energy/get_prefs");
    send_json(obj);
}

static void subscribe_sub(int slot, int sub)
{
    int id = s_next_id++;
    s_slots[slot].subs[sub].subscribe_id = id;
    cJSON *trigger = cJSON_CreateObject();
    cJSON_AddStringToObject(trigger, "platform", "state");
    cJSON_AddStringToObject(trigger, "entity_id", s_slots[slot].subs[sub].entity_id);
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", id);
    cJSON_AddStringToObject(obj, "type", "subscribe_trigger");
    cJSON_AddItemToObject(obj, "trigger", trigger);
    send_json(obj);
}

static void bootstrap_sub(int slot, int sub, const char *url, const char *token)
{
    sub_entity_t *s = &s_slots[slot].subs[sub];
    float value = 0.0f;
    ha_entity_status_t st = ha_client_get_entity_power(url, token, s->entity_id, &value);
    if (st == HA_ENTITY_STATUS_OK) {
        s->power_w = value;
        s->last_sample_us = esp_timer_get_time();
        s->live = true;
    } else {
        ESP_LOGW(TAG, "bootstrap read of %s failed (status %d)", s->entity_id, (int)st);
    }
}

typedef struct {
    char id[HA_WS_ENTITY_ID_LEN];
} cand_id_t;

static bool add_candidate(cand_id_t list[], int *count, const char *id)
{
    if (id == NULL || id[0] == '\0' || *count >= HA_WS_MAX_SUB) {
        return false;
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(list[i].id, id) == 0) {
            return true; /* already have it - HA shouldn't repeat an id, but don't double-count if it did */
        }
    }
    strncpy(list[*count].id, id, HA_WS_ENTITY_ID_LEN - 1);
    list[*count].id[HA_WS_ENTITY_ID_LEN - 1] = '\0';
    (*count)++;
    return true;
}

/* Rebuilds s_slots[] from a fresh energy/get_prefs "result" object, then
 * (re)subscribes and bootstraps every new entity. Power only - Home
 * Assistant's Energy Dashboard's cumulative kWh statistics are never
 * used, not even as a fallback: a ring with no live power entity linked
 * in HA is simply not configured, full stop. A ring can still be fed by
 * more than one power entity (e.g. a dual-tariff grid meter that happens
 * to expose two live power sensors) - every one found is subscribed and
 * summed. Slots whose entity list didn't change keep their running
 * power, so a rescan doesn't blank the rings for no reason. */
static void apply_energy_prefs(const cJSON *result, const char *url, const char *token)
{
    if (result == NULL) {
        return;
    }

    /* Heap-allocated rather than a stack local, same lesson as the
     * wifi_ap_record_t stack overflow documented in
     * docs/HARDWARE_REFERENCE.md (this one hit for real, on hardware,
     * the first time this function grew large locals). */
    cand_id_t (*cand)[HA_WS_MAX_SUB] = calloc(SLOT_COUNT, sizeof(*cand));
    int cand_counts[SLOT_COUNT] = {0};
    if (cand == NULL) {
        ESP_LOGE(TAG, "oom parsing energy prefs");
        return;
    }

    const cJSON *sources = cJSON_GetObjectItemCaseSensitive(result, "energy_sources");
    const cJSON *src = NULL;
    int source_count = 0;
    cJSON_ArrayForEach(src, sources) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(src, "type");
        const char *type_s = (cJSON_IsString(type) && type->valuestring != NULL) ? type->valuestring : "?";
        ESP_LOGI(TAG, "energy_sources[%d].type = %s", source_count, type_s);
        source_count++;

        /* Real-world schema (verified live against the user's own Home
         * Assistant instance, not just the API docs): a source's live
         * power sensor - when Home Assistant's Energy Dashboard UI has
         * one linked - shows up as "stat_rate" (solar) or
         * "power_config.stat_rate_from"/"stat_rate_to" (grid, battery).
         * These are exactly the entities the parent Helios HA card's own
         * power chips read, and the *only* thing this component ever
         * uses - the cumulative kWh statistics that always exist
         * alongside them are for HA's own billing/history graphs, not
         * for driving a live display, and are never touched here. */
        if (strcmp(type_s, "solar") == 0) {
            const cJSON *rate = cJSON_GetObjectItemCaseSensitive(src, "stat_rate");
            if (cJSON_IsString(rate) && rate->valuestring[0] != '\0') {
                add_candidate(cand[SLOT_SOLAR], &cand_counts[SLOT_SOLAR], rate->valuestring);
            }
        } else if (strcmp(type_s, "grid") == 0) {
            const cJSON *power_config = cJSON_GetObjectItemCaseSensitive(src, "power_config");
            const cJSON *rate_from = cJSON_GetObjectItemCaseSensitive(power_config, "stat_rate_from");
            const cJSON *rate_to = cJSON_GetObjectItemCaseSensitive(power_config, "stat_rate_to");
            if (cJSON_IsString(rate_from) && rate_from->valuestring[0] != '\0') {
                add_candidate(cand[SLOT_GRID_IMPORT], &cand_counts[SLOT_GRID_IMPORT], rate_from->valuestring);
            }
            if (cJSON_IsString(rate_to) && rate_to->valuestring[0] != '\0') {
                add_candidate(cand[SLOT_GRID_EXPORT], &cand_counts[SLOT_GRID_EXPORT], rate_to->valuestring);
            }
        } else if (strcmp(type_s, "battery") == 0) {
            const cJSON *power_config = cJSON_GetObjectItemCaseSensitive(src, "power_config");
            const cJSON *rate_from = cJSON_GetObjectItemCaseSensitive(power_config, "stat_rate_from");
            const cJSON *rate_to = cJSON_GetObjectItemCaseSensitive(power_config, "stat_rate_to");
            if (cJSON_IsString(rate_from) && rate_from->valuestring[0] != '\0') {
                add_candidate(cand[SLOT_BATTERY_DISCHARGE], &cand_counts[SLOT_BATTERY_DISCHARGE], rate_from->valuestring);
            }
            if (cJSON_IsString(rate_to) && rate_to->valuestring[0] != '\0') {
                add_candidate(cand[SLOT_BATTERY_CHARGE], &cand_counts[SLOT_BATTERY_CHARGE], rate_to->valuestring);
            }
        }
    }
    ESP_LOGI(TAG, "energy_sources array had %d entries total", source_count);

    bool any_configured = false;
    for (int i = 0; i < SLOT_COUNT; i++) {
        any_configured = any_configured || cand_counts[i] > 0;

        bool changed = cand_counts[i] != s_slots[i].sub_count;
        if (!changed) {
            for (int j = 0; j < cand_counts[i]; j++) {
                if (strcmp(cand[i][j].id, s_slots[i].subs[j].entity_id) != 0) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) {
            continue; /* same set of sources as before - keep the running power */
        }

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            memset(&s_slots[i], 0, sizeof(s_slots[i]));
            s_slots[i].sub_count = cand_counts[i];
            for (int j = 0; j < cand_counts[i]; j++) {
                strncpy(s_slots[i].subs[j].entity_id, cand[i][j].id, HA_WS_ENTITY_ID_LEN - 1);
            }
            xSemaphoreGive(s_mutex);
        }
    }
    free(cand);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_prefs_loaded = true;
        s_energy_dashboard_configured = any_configured;
        xSemaphoreGive(s_mutex);
    }

    ESP_LOGI(TAG, "energy prefs (power entities only): solar=%d grid_in=%d grid_out=%d bat_chg=%d bat_dis=%d",
             s_slots[SLOT_SOLAR].sub_count, s_slots[SLOT_GRID_IMPORT].sub_count,
             s_slots[SLOT_GRID_EXPORT].sub_count, s_slots[SLOT_BATTERY_CHARGE].sub_count,
             s_slots[SLOT_BATTERY_DISCHARGE].sub_count);

    for (int i = 0; i < SLOT_COUNT; i++) {
        for (int j = 0; j < s_slots[i].sub_count; j++) {
            if (s_slots[i].subs[j].subscribe_id == 0) {
                subscribe_sub(i, j);
                if (!s_slots[i].subs[j].live) {
                    bootstrap_sub(i, j, url, token);
                }
            }
        }
    }
}

static bool find_sub_by_subscribe_id(int id, int *out_slot, int *out_sub)
{
    if (id == 0) {
        return false;
    }
    for (int i = 0; i < SLOT_COUNT; i++) {
        for (int j = 0; j < s_slots[i].sub_count; j++) {
            if (s_slots[i].subs[j].subscribe_id == id) {
                *out_slot = i;
                *out_sub = j;
                return true;
            }
        }
    }
    return false;
}

static void handle_result(cJSON *root)
{
    const cJSON *id_node = cJSON_GetObjectItemCaseSensitive(root, "id");
    int id = cJSON_IsNumber(id_node) ? id_node->valueint : -1;
    const cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
    bool ok = cJSON_IsTrue(success);

    if (id != 0 && id == s_prefs_request_id) {
        if (ok) {
            /* url/token are re-loaded here rather than threaded all the way
             * through process_message() - a handful of extra NVS reads is
             * cheap and keeps the call chain simple. */
            char url[HA_WS_URL_LEN] = {0};
            char token[HA_WS_TOKEN_LEN] = {0};
            load_ha_credentials(url, sizeof(url), token, sizeof(token));
            apply_energy_prefs(cJSON_GetObjectItemCaseSensitive(root, "result"), url, token);
        } else {
            ESP_LOGW(TAG, "energy/get_prefs request failed");
        }
        return;
    }

    if (!ok) {
        int slot, sub;
        if (find_sub_by_subscribe_id(id, &slot, &sub)) {
            ESP_LOGW(TAG, "subscribe_trigger failed for %s", s_slots[slot].subs[sub].entity_id);
        }
    }
}

static void handle_trigger_event(cJSON *root)
{
    const cJSON *id_node = cJSON_GetObjectItemCaseSensitive(root, "id");
    int id = cJSON_IsNumber(id_node) ? id_node->valueint : -1;

    int slot = -1;
    int sub = -1;
    if (!find_sub_by_subscribe_id(id, &slot, &sub)) {
        return;
    }

    const cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
    const cJSON *variables = cJSON_GetObjectItemCaseSensitive(event, "variables");
    const cJSON *trigger = cJSON_GetObjectItemCaseSensitive(variables, "trigger");
    const cJSON *to_state = cJSON_GetObjectItemCaseSensitive(trigger, "to_state");
    if (to_state == NULL) {
        return;
    }
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(to_state, "state");
    if (!cJSON_IsString(state) || state->valuestring == NULL) {
        return;
    }
    if (strcmp(state->valuestring, "unavailable") == 0 || strcmp(state->valuestring, "unknown") == 0) {
        return; /* transient - keep whatever the last good sample was */
    }

    char *end = NULL;
    float new_value = strtof(state->valuestring, &end);
    if (end == state->valuestring) {
        return; /* not numeric */
    }

    /* Same kW/MW -> W conversion ha_client_get_entity_power() applies to
     * the REST bootstrap of this same entity. */
    float multiplier = 1.0f;
    const cJSON *attrs = cJSON_GetObjectItemCaseSensitive(to_state, "attributes");
    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
        if (strcasecmp(unit->valuestring, "kW") == 0) {
            multiplier = 1000.0f;
        } else if (strcasecmp(unit->valuestring, "MW") == 0) {
            multiplier = 1000000.0f;
        }
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        sub_entity_t *s = &s_slots[slot].subs[sub];
        s->power_w = new_value * multiplier;
        s->last_sample_us = esp_timer_get_time();
        s->live = true;
        xSemaphoreGive(s_mutex);
    }
}

static void process_message(const char *msg)
{
    cJSON *root = cJSON_Parse(msg);
    if (root == NULL) {
        ESP_LOGW(TAG, "unparseable message (%d bytes)", (int)strlen(msg));
        return;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const char *type_s = (cJSON_IsString(type) && type->valuestring != NULL) ? type->valuestring : "";

    if (strcmp(type_s, "auth_required") == 0) {
        char token[HA_WS_TOKEN_LEN] = {0};
        strncpy(token, s_started_token, sizeof(token) - 1);
        send_auth(token);
    } else if (strcmp(type_s, "auth_ok") == 0) {
        s_authenticated = true;
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            s_link = HA_WS_LINK_CONNECTED;
            xSemaphoreGive(s_mutex);
        }
        ESP_LOGI(TAG, "authenticated");
        request_prefs();
    } else if (strcmp(type_s, "auth_invalid") == 0) {
        s_authenticated = false;
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            s_link = HA_WS_LINK_AUTH_FAILED;
            xSemaphoreGive(s_mutex);
        }
        ESP_LOGW(TAG, "auth_invalid - token rejected");
    } else if (strcmp(type_s, "result") == 0) {
        handle_result(root);
    } else if (strcmp(type_s, "event") == 0) {
        handle_trigger_event(root);
    }

    cJSON_Delete(root);
}

static void reset_fragment_buffer(void)
{
    free(s_frag_buf);
    s_frag_buf = NULL;
    s_frag_total = 0;
    s_frag_filled = 0;
}

static void handle_ws_data(const esp_websocket_event_data_t *data)
{
    if (data->data_len <= 0) {
        return;
    }
    if (data->payload_offset == 0) {
        reset_fragment_buffer();
        s_frag_total = data->payload_len > 0 ? data->payload_len : data->data_len;
        s_frag_buf = malloc((size_t)s_frag_total + 1);
        if (s_frag_buf == NULL) {
            ESP_LOGW(TAG, "oom reassembling a %d-byte websocket frame", s_frag_total);
            return;
        }
        s_frag_filled = 0;
    }
    if (s_frag_buf == NULL) {
        return; /* allocation failed for this frame's first fragment - drop the rest of it too */
    }

    int copy_len = data->data_len;
    if (s_frag_filled + copy_len > s_frag_total) {
        copy_len = s_frag_total - s_frag_filled;
    }
    if (copy_len > 0) {
        memcpy(s_frag_buf + s_frag_filled, data->data_ptr, (size_t)copy_len);
        s_frag_filled += copy_len;
    }

    if (s_frag_filled >= s_frag_total) {
        s_frag_buf[s_frag_filled] = '\0';
        char *msg = s_frag_buf;
        s_frag_buf = NULL;
        s_frag_total = 0;
        s_frag_filled = 0;

        if (s_rx_queue == NULL || xQueueSend(s_rx_queue, &msg, 0) != pdTRUE) {
            free(msg); /* queue missing or full - drop rather than block the client's own task */
        }
    }
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "transport connected, waiting for Home Assistant to authenticate us");
            s_authenticated = false;
            for (int i = 0; i < SLOT_COUNT; i++) {
                for (int j = 0; j < s_slots[i].sub_count; j++) {
                    s_slots[i].subs[j].subscribe_id = 0; /* subscriptions don't survive a reconnect */
                }
            }
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_link = HA_WS_LINK_CONNECTING;
                xSemaphoreGive(s_mutex);
            }
            reset_fragment_buffer();
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGW(TAG, "websocket disconnected/error - will auto-reconnect");
            s_authenticated = false;
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_link = HA_WS_LINK_DISCONNECTED;
                xSemaphoreGive(s_mutex);
            }
            reset_fragment_buffer();
            break;
        case WEBSOCKET_EVENT_DATA:
            handle_ws_data((const esp_websocket_event_data_t *)event_data);
            break;
        default:
            break;
    }
}

static void start_or_restart_client(const char *url, const char *token)
{
    if (s_client != NULL) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }

    strncpy(s_started_url, url, sizeof(s_started_url) - 1);
    s_started_url[sizeof(s_started_url) - 1] = '\0';
    strncpy(s_started_token, token, sizeof(s_started_token) - 1);
    s_started_token[sizeof(s_started_token) - 1] = '\0';

    char ws_url[HA_WS_URL_LEN + 32];
    build_ws_url(url, ws_url, sizeof(ws_url));
    ESP_LOGI(TAG, "connecting to %s", ws_url);

    esp_websocket_client_config_t cfg = {
        .uri = ws_url,
        .buffer_size = HA_WS_WS_BUFFER_SIZE,
        .task_stack = 6144,
        .reconnect_timeout_ms = 8000,
        .network_timeout_ms = 10000,
    };
    s_client = esp_websocket_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_websocket_client_init failed");
        return;
    }
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(s_client);
}

static void ha_ws_task(void *arg)
{
    (void)arg;
    for (;;) {
        char url[HA_WS_URL_LEN] = {0};
        char token[HA_WS_TOKEN_LEN] = {0};
        bool have_creds = load_ha_credentials(url, sizeof(url), token, sizeof(token));

        if (!have_creds) {
            if (s_client != NULL) {
                esp_websocket_client_stop(s_client);
                esp_websocket_client_destroy(s_client);
                s_client = NULL;
            }
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_link = HA_WS_LINK_DISCONNECTED;
                xSemaphoreGive(s_mutex);
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        bool url_or_token_changed = strcmp(url, s_started_url) != 0 || strcmp(token, s_started_token) != 0;
        if (s_client == NULL || url_or_token_changed) {
            start_or_restart_client(url, token);
        }

        char *msg = NULL;
        if (xQueueReceive(s_rx_queue, &msg, pdMS_TO_TICKS(HA_WS_WAIT_MS)) == pdTRUE) {
            process_message(msg);
            free(msg);
        }

        if (xSemaphoreTake(s_rescan_sem, 0) == pdTRUE && s_authenticated) {
            ESP_LOGI(TAG, "rescan requested");
            request_prefs();
        }
    }
}

void ha_ws_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_rescan_sem = xSemaphoreCreateBinary();
    s_rx_queue = xQueueCreate(8, sizeof(char *));
    xTaskCreate(ha_ws_task, "ha_ws", HA_WS_TASK_STACK, NULL, HA_WS_TASK_PRIORITY, NULL);
}

void ha_ws_rescan(void)
{
    if (s_rescan_sem != NULL) {
        xSemaphoreGive(s_rescan_sem);
    }
}

/* Joins every contributing entity id (comma-separated) for display, and
 * derives the ring's live status/power by summing whichever contributors
 * have a usable reading so far - a source fed by several sensors only has
 * one of them actively updating at any given moment (e.g. a redundant
 * pairing), so staleness uses the freshest contributor rather than the
 * oldest. */
static void fill_source(ha_ws_source_t *out, const slot_state_t *s, int64_t now_us)
{
    out->entity_id[0] = '\0';
    for (int i = 0; i < s->sub_count; i++) {
        if (i > 0) {
            strncat(out->entity_id, ", ", sizeof(out->entity_id) - strlen(out->entity_id) - 1);
        }
        strncat(out->entity_id, s->subs[i].entity_id, sizeof(out->entity_id) - strlen(out->entity_id) - 1);
    }

    if (s->sub_count == 0) {
        out->status = HA_WS_SOURCE_NOT_CONFIGURED;
        return;
    }

    float total_power = 0.0f;
    int64_t min_age_ms = -1;
    bool any_live = false;
    for (int i = 0; i < s->sub_count; i++) {
        const sub_entity_t *sub = &s->subs[i];
        if (!sub->live) {
            continue;
        }
        any_live = true;
        total_power += sub->power_w;
        int64_t age_ms = (now_us - sub->last_sample_us) / 1000;
        if (min_age_ms < 0 || age_ms < min_age_ms) {
            min_age_ms = age_ms;
        }
    }

    if (!any_live) {
        out->status = HA_WS_SOURCE_WAITING;
        return;
    }

    out->power_w = total_power;
    out->age_ms = min_age_ms;
    out->status = (min_age_ms > (int64_t)HA_WS_STALE_AFTER_S * 1000) ? HA_WS_SOURCE_STALE : HA_WS_SOURCE_LIVE;
}

void ha_ws_get_status(ha_ws_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    out->link = s_link;
    out->prefs_loaded = s_prefs_loaded;
    out->energy_dashboard_configured = s_energy_dashboard_configured;

    int64_t now_us = esp_timer_get_time();
    fill_source(&out->solar, &s_slots[SLOT_SOLAR], now_us);
    fill_source(&out->grid_import, &s_slots[SLOT_GRID_IMPORT], now_us);
    fill_source(&out->grid_export, &s_slots[SLOT_GRID_EXPORT], now_us);
    fill_source(&out->battery_charge, &s_slots[SLOT_BATTERY_CHARGE], now_us);
    fill_source(&out->battery_discharge, &s_slots[SLOT_BATTERY_DISCHARGE], now_us);

    xSemaphoreGive(s_mutex);
}
