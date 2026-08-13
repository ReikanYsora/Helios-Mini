#pragma once

#include <stddef.h>
#include "esp_err.h"

/* Initializes NVS flash, erasing and retrying once on a version-mismatch or
 * out-of-space error (the standard ESP-IDF recovery pattern). Call once,
 * early in app_main(). */
esp_err_t storage_init(void);

esp_err_t storage_get_string(const char *namespace_name, const char *key, char *out, size_t out_size);
esp_err_t storage_set_string(const char *namespace_name, const char *key, const char *value);

/* Erases every key in namespace_name - Wi-Fi credentials, Home Assistant
 * URL/token, ring limits, page toggles, MQTT config, everything this app
 * has ever written lives in one "helios_mini" namespace, so this is the
 * whole factory-reset story. Caller reboots (esp_restart()) afterwards -
 * nothing here does that itself. */
esp_err_t storage_erase_all(const char *namespace_name);
