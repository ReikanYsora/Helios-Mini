#include "helios_config.h"
#include "storage.h"

#include "esp_err.h"

bool helios_config_get_ha_credentials(char *url, size_t url_len,
                                      char *token, size_t token_len)
{
    url[0] = '\0';
    token[0] = '\0';
    bool have_url = storage_get_string(HELIOS_NVS_NAMESPACE, HELIOS_NVS_KEY_HA_URL,
                                       url, url_len) == ESP_OK && url[0] != '\0';
    bool have_token = storage_get_string(HELIOS_NVS_NAMESPACE, HELIOS_NVS_KEY_HA_TOKEN,
                                         token, token_len) == ESP_OK && token[0] != '\0';
    return have_url && have_token;
}
