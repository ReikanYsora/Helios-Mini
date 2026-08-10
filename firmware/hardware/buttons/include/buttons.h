#pragma once

typedef enum {
    HELIOS_BUTTON_BOOT,
    HELIOS_BUTTON_PWR,
} helios_button_id_t;

typedef enum {
    HELIOS_BUTTON_EVENT_SINGLE_CLICK,
    HELIOS_BUTTON_EVENT_DOUBLE_CLICK,
    HELIOS_BUTTON_EVENT_LONG_PRESS,
} helios_button_event_t;

typedef void (*helios_button_cb_t)(helios_button_id_t id, helios_button_event_t event, void *user_ctx);

/* Initializes both BOOT and PWR buttons (debounced via the espressif/button
 * managed component) and registers a single callback for all their events. */
void buttons_init(helios_button_cb_t cb, void *user_ctx);
