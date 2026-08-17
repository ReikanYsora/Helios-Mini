#include "diagnostics.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "diagnostics";

#define DIAGNOSTICS_SAMPLE_MS       5000  /* one history sample per tick */
#define DIAGNOSTICS_LOG_INTERVAL_MS 30000 /* full status line every 6 ticks */
#define DIAGNOSTICS_TASK_STACK      3072
#define DIAGNOSTICS_TASK_PRIORITY   1

static temperature_sensor_handle_t s_temp_sensor = NULL;
static float s_temp_last = 0.0f;

/* Per-core CPU load from the idle tasks' run-time counters (esp_timer us):
 * load = 1 - (idle_delta / elapsed). */
static uint32_t s_last_idle[2];
static uint32_t s_last_stamp_us;
static float s_cpu_last[2];

/* Rolling history for the /debug graphs. Written only by diagnostics_task,
 * read by the HTTP task via diagnostics_get_history; the short copy is taken
 * under this spinlock. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static diagnostics_sample_t s_hist[DIAGNOSTICS_HISTORY];
static size_t s_head;  /* next write slot */
static size_t s_count; /* valid samples, saturates at DIAGNOSTICS_HISTORY */

static float read_die_temperature(void)
{
    float t;
    if (s_temp_sensor && temperature_sensor_get_celsius(s_temp_sensor, &t) == ESP_OK) {
        s_temp_last = t;
    }
    return s_temp_last;
}

/* Updates s_cpu_last[] over the interval since the previous call. Unsigned
 * 32-bit deltas are wrap-safe (both counters share the esp_timer base). */
static void sample_cpu(void)
{
    uint32_t now = (uint32_t)esp_timer_get_time();
    uint32_t elapsed = now - s_last_stamp_us;
    for (int core = 0; core < 2; core++) {
        uint32_t idle = (uint32_t)ulTaskGetIdleRunTimeCounterForCore(core);
        uint32_t d = idle - s_last_idle[core];
        float load = elapsed ? 100.0f * (1.0f - (float)d / (float)elapsed) : 0.0f;
        if (load < 0.0f) load = 0.0f;
        if (load > 100.0f) load = 100.0f;
        s_cpu_last[core] = load;
        s_last_idle[core] = idle;
    }
    s_last_stamp_us = now;
}

static void push_sample(void)
{
    diagnostics_sample_t s = {
        .temperature_c = s_temp_last,
        .cpu0_pct = s_cpu_last[0],
        .cpu1_pct = s_cpu_last[1],
        .free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        .free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
    };
    wifi_ap_record_t ap;
    s.wifi_rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;

    portENTER_CRITICAL(&s_mux);
    s_hist[s_head] = s;
    s_head = (s_head + 1) % DIAGNOSTICS_HISTORY;
    if (s_count < DIAGNOSTICS_HISTORY) {
        s_count++;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void diagnostics_task(void *arg)
{
    uint32_t ticks_since_log = 0;
    const uint32_t ticks_per_log = DIAGNOSTICS_LOG_INTERVAL_MS / DIAGNOSTICS_SAMPLE_MS;
    s_last_stamp_us = (uint32_t)esp_timer_get_time();
    while (1) {
        read_die_temperature();
        sample_cpu();
        push_sample();

        if (ticks_since_log == 0) {
            ESP_LOGI(TAG, "uptime=%llds heap=%u psram=%u die=%.1fC cpu0=%.0f%% cpu1=%.0f%%",
                     (long long)(esp_timer_get_time() / 1000000),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     s_temp_last, s_cpu_last[0], s_cpu_last[1]);
        }
        ticks_since_log = (ticks_since_log + 1) % ticks_per_log;
        vTaskDelay(pdMS_TO_TICKS(DIAGNOSTICS_SAMPLE_MS));
    }
}

void diagnostics_start(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    if (temperature_sensor_install(&cfg, &s_temp_sensor) == ESP_OK) {
        temperature_sensor_enable(s_temp_sensor);
    } else {
        ESP_LOGW(TAG, "die temperature sensor unavailable");
        s_temp_sensor = NULL;
    }
    xTaskCreate(diagnostics_task, "diagnostics", DIAGNOSTICS_TASK_STACK, NULL, DIAGNOSTICS_TASK_PRIORITY, NULL);
}

void diagnostics_get_status(diagnostics_status_t *out)
{
    out->uptime_s = esp_timer_get_time() / 1000000;
    out->free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    wifi_ap_record_t ap_info;
    out->wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    out->wifi_rssi = out->wifi_connected ? ap_info.rssi : 0;

    /* Latest values sampled by diagnostics_task (<=5 s old) rather than a
     * fresh read here: keeps all sensor access on the one task. */
    out->temperature_c = s_temp_last;
    out->cpu0_pct = s_cpu_last[0];
    out->cpu1_pct = s_cpu_last[1];
}

void diagnostics_get_history(diagnostics_sample_t *out, size_t max, size_t *out_count)
{
    portENTER_CRITICAL(&s_mux);
    size_t n = s_count < max ? s_count : max;
    size_t start = (s_head + DIAGNOSTICS_HISTORY - s_count) % DIAGNOSTICS_HISTORY;
    start = (start + (s_count - n)) % DIAGNOSTICS_HISTORY; /* keep newest n if capped */
    for (size_t i = 0; i < n; i++) {
        out[i] = s_hist[(start + i) % DIAGNOSTICS_HISTORY];
    }
    portEXIT_CRITICAL(&s_mux);
    *out_count = n;
}
