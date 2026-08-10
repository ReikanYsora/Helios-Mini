/* Wraps the espressif/button managed component for the BOOT (GPIO0) and
 * PWR (GPIO17) buttons. Its exact API (iot_button.h struct/function names)
 * has not been compiled against yet in this repo - verify against the
 * resolved component version on first build. See
 * docs/HARDWARE_REFERENCE.md. */
#include "buttons.h"
#include "board_config.h"
#include "iot_button.h"
#include "esp_log.h"

static const char *TAG = "buttons";
static helios_button_cb_t s_cb = NULL;
static void *s_user_ctx = NULL;

typedef struct {
    helios_button_id_t id;
    helios_button_event_t event;
} button_event_ctx_t;

/* One context per (button, event) pair we care about; must outlive the
 * button handles, so static storage. */
static button_event_ctx_t s_single_click_ctx[2];
static button_event_ctx_t s_double_click_ctx[2];
static button_event_ctx_t s_long_press_ctx[2];

static void on_button_event(void *button_handle, void *user_data)
{
    button_event_ctx_t *ctx = (button_event_ctx_t *)user_data;
    if (s_cb) {
        s_cb(ctx->id, ctx->event, s_user_ctx);
    }
}

static void register_button(int gpio, helios_button_id_t id)
{
    button_config_t cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = gpio,
            .active_level = 0,
        },
    };
    button_handle_t handle = iot_button_create(&cfg);
    if (handle == NULL) {
        ESP_LOGE(TAG, "failed to create button on GPIO%d", gpio);
        return;
    }

    s_single_click_ctx[id] = (button_event_ctx_t){ .id = id, .event = HELIOS_BUTTON_EVENT_SINGLE_CLICK };
    s_double_click_ctx[id] = (button_event_ctx_t){ .id = id, .event = HELIOS_BUTTON_EVENT_DOUBLE_CLICK };
    s_long_press_ctx[id]   = (button_event_ctx_t){ .id = id, .event = HELIOS_BUTTON_EVENT_LONG_PRESS };

    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, on_button_event, &s_single_click_ctx[id]);
    iot_button_register_cb(handle, BUTTON_DOUBLE_CLICK, on_button_event, &s_double_click_ctx[id]);
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, on_button_event, &s_long_press_ctx[id]);
}

void buttons_init(helios_button_cb_t cb, void *user_ctx)
{
    s_cb = cb;
    s_user_ctx = user_ctx;
    register_button(HELIOS_PIN_BUTTON_BOOT, HELIOS_BUTTON_BOOT);
    register_button(HELIOS_PIN_BUTTON_PWR, HELIOS_BUTTON_PWR);
}
