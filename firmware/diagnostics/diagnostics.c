#include "diagnostics.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
