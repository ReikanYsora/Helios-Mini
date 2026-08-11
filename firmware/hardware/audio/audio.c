#include "audio.h"
#include "board_config.h"
#include "i2c_bus.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "es8311.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <math.h>

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx_chan = NULL;
static i2s_chan_handle_t s_rx_chan = NULL;

#define HELIOS_AUDIO_SAMPLE_RATE_HZ 16000

/* Startup chime: verifies the speaker path end-to-end without needing an
 * audio asset - a short tone with a fade-in/out envelope (no clicks). */
#define STARTUP_TONE_FREQ_HZ   880
#define STARTUP_TONE_MS        180
#define STARTUP_TONE_AMPLITUDE 6000  /* well under INT16_MAX, avoids clipping */
#define STARTUP_TONE_TWO_PI    6.283185307f

void audio_pa_enable(bool enable)
{
    gpio_set_level(HELIOS_PIN_AUDIO_PA_EN, enable ? 1 : 0);
}

static void i2s_init(void)
{
    gpio_config_t pa_cfg = {
        .pin_bit_mask = 1ULL << HELIOS_PIN_AUDIO_PA_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&pa_cfg));
    audio_pa_enable(false);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(HELIOS_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = HELIOS_PIN_I2S_MCLK,
            .bclk = HELIOS_PIN_I2S_BCLK,
            .ws = HELIOS_PIN_I2S_WS,
            .dout = HELIOS_PIN_I2S_DOUT,
            .din = HELIOS_PIN_I2S_DIN,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));
}

/* Thin wrapper around the espressif/es8311 managed component (verified
 * against the resolved 1.0.0~1: es8311_create() takes the legacy
 * i2c_port_t, which is why hardware/i2c_bus and hardware/touch use the
 * legacy driver/i2c.h too - see board_config.h and
 * docs/HARDWARE_REFERENCE.md. */
static bool codec_init(void)
{
    es8311_handle_t codec = es8311_create(i2c_bus_get_port(), ES8311_ADDRRES_0);
    if (codec == NULL) {
        ESP_LOGE(TAG, "es8311_create failed");
        return false;
    }

    es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = HELIOS_AUDIO_SAMPLE_RATE_HZ * 256,
        .sample_frequency = HELIOS_AUDIO_SAMPLE_RATE_HZ,
    };
    if (es8311_init(codec, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        ESP_LOGE(TAG, "es8311_init failed");
        return false;
    }

    es8311_voice_volume_set(codec, 70, NULL);
    es8311_voice_mute(codec, false);
    es8311_microphone_config(codec, false);
    return true;
}

bool audio_init(void)
{
    i2s_init();
    return codec_init();
}

void audio_play_startup_tone(void)
{
    const int sample_rate = HELIOS_AUDIO_SAMPLE_RATE_HZ;
    const int num_samples = sample_rate * STARTUP_TONE_MS / 1000;

    int16_t *samples = malloc(num_samples * sizeof(int16_t));
    if (samples == NULL) {
        ESP_LOGW(TAG, "startup tone: allocation failed, skipping");
        return;
    }

    for (int i = 0; i < num_samples; i++) {
        float t = (float)i / (float)num_samples;
        /* Linear fade-in/out over the first/last 10% to avoid a click. */
        float envelope = (t < 0.1f) ? (t / 0.1f) : (t > 0.9f) ? ((1.0f - t) / 0.1f) : 1.0f;
        float sample = STARTUP_TONE_AMPLITUDE * envelope *
                       sinf(STARTUP_TONE_TWO_PI * STARTUP_TONE_FREQ_HZ * i / sample_rate);
        samples[i] = (int16_t)sample;
    }

    audio_pa_enable(true);
    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx_chan, samples, num_samples * sizeof(int16_t),
                                       &bytes_written, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "startup tone: i2s_channel_write failed (%s)", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(50)); /* let the amp settle before cutting power */
    audio_pa_enable(false);

    free(samples);
}
