#include "diagnostics.h"
#include "display.h"
#include "audio.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "diagnostics";

#define DIAGNOSTICS_LOG_INTERVAL_MS 30000
#define DIAGNOSTICS_TASK_STACK      3072
#define DIAGNOSTICS_TASK_PRIORITY   1

static void diagnostics_task(void *arg)
{
    while (1) {
        int64_t uptime_s = esp_timer_get_time() / 1000000;
        ESP_LOGI(TAG, "uptime=%llds free_heap=%u free_psram=%u",
                 (long long)uptime_s,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(DIAGNOSTICS_LOG_INTERVAL_MS));
    }
}

void diagnostics_start(void)
{
    xTaskCreate(diagnostics_task, "diagnostics", DIAGNOSTICS_TASK_STACK, NULL, DIAGNOSTICS_TASK_PRIORITY, NULL);
}

void diagnostics_get_status(diagnostics_status_t *out)
{
    out->uptime_s = esp_timer_get_time() / 1000000;
    out->free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    wifi_ap_record_t ap_info;
    out->wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    out->wifi_rssi = out->wifi_connected ? ap_info.rssi : 0;
}

void diagnostics_test_screen(void)
{
    lv_color_t colors[] = {
        lv_color_hex(0xff0000),
        lv_color_hex(0x00ff00),
        lv_color_hex(0x0000ff),
        lv_color_white(),
    };
    /* Lock/unlock around each individual change (rather than for the whole
     * sequence) so the display component's own lvgl task gets a chance to
     * actually flush each color to the panel in between - see
     * docs/HARDWARE_REFERENCE.md on LVGL's threading contract. */
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        if (display_lock(0)) {
            lv_obj_set_style_bg_color(lv_screen_active(), colors[i], 0);
            display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    if (display_lock(0)) {
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
        display_unlock();
    }
}

void diagnostics_test_speaker(void)
{
    audio_play_startup_tone();
}

int16_t diagnostics_test_microphone(void)
{
    return audio_measure_mic_level();
}
