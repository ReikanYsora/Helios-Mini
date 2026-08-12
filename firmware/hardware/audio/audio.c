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

/* Startup chime: a short rising arpeggio (no audio asset), synthesized
 * directly - meant to sound like a considered product sound, not a debug
 * beep. Also verifies the speaker path end-to-end.
 *
 * Notes play one at a time, never stacked: an earlier version played all
 * four as a simultaneous chord and it came out sounding like a ship's
 * foghorn on this speaker - the ~80-100Hz beat frequencies between
 * simultaneous close-together tones (349 vs 440Hz beats at 91Hz, etc.) are
 * exactly the "foghorn wah" range, and a small single-driver speaker has
 * no headroom to keep that clean. A sequential arpeggio never has more
 * than one tone sounding, so there's nothing to beat against. */
#define STARTUP_CHIME_TWO_PI 6.283185307f
#define STARTUP_CHIME_PI     3.14159265f
#define STARTUP_CHIME_ATTACK_MS 12.0f    /* every note's own fade-in */
#define STARTUP_CHIME_AMPLITUDE 22000.0f /* single voice at a time - more headroom than a stack needs */

typedef struct {
    float freq_hz;
    float duration_ms;
    float release_ms; /* fade-out, ends exactly at duration_ms - must be <= duration_ms */
} chime_note_t;

/* Rising F major run - root, third, fifth, then a held octave "landing". */
static const chime_note_t CHIME_NOTES[] = {
    { 349.23f, 130.0f,  25.0f }, /* F4 */
    { 440.00f, 130.0f,  25.0f }, /* A4 */
    { 523.25f, 140.0f,  30.0f }, /* C5 */
    { 698.46f, 420.0f, 260.0f }, /* F5 - held, graceful fade */
};
#define CHIME_NOTE_COUNT (sizeof(CHIME_NOTES) / sizeof(CHIME_NOTES[0]))

/* Smooth 0->1 ramp (raised cosine) over x in [0,1] - no linear-ramp
 * clicks/harshness. */
static float chime_raised_cosine(float x)
{
    if (x <= 0.0f) {
        return 0.0f;
    }
    if (x >= 1.0f) {
        return 1.0f;
    }
    return (1.0f - cosf(STARTUP_CHIME_PI * x)) * 0.5f;
}

/* 0 at local_ms=0, 1 through the sustain, back to 0 exactly at
 * local_ms=duration_ms - each note fully resolves before the next starts. */
static float chime_note_envelope(float local_ms, float duration_ms, float release_ms)
{
    if (local_ms < STARTUP_CHIME_ATTACK_MS) {
        return chime_raised_cosine(local_ms / STARTUP_CHIME_ATTACK_MS);
    }
    float release_start = duration_ms - release_ms;
    if (local_ms >= release_start) {
        return chime_raised_cosine((duration_ms - local_ms) / release_ms);
    }
    return 1.0f;
}

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

    /* Stereo slot mode, not mono: the ES8311 (like most cheap I2S DAC/ADC
     * codecs) expects a full stereo Philips frame - the ESP-IDF I2S
     * peripheral's "mono slot" mode only sends one slot per LRCK cycle,
     * which this codec doesn't handle and produces silence. Waveshare's
     * own reference for this exact board configures the codec with
     * channel=2 for the same reason. Mono audio is sent duplicated onto
     * both L/R slots instead - see audio_play_startup_tone(). */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(HELIOS_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
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

    es8311_voice_volume_set(codec, 85, NULL); /* up from the original 70 (quiet), back off 92 (distorted) */
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

    float total_ms = 0.0f;
    for (size_t n = 0; n < CHIME_NOTE_COUNT; n++) {
        total_ms += CHIME_NOTES[n].duration_ms;
    }
    const int num_samples = (int)(sample_rate * total_ms / 1000.0f);

    /* Stereo-interleaved (L,R,L,R,...): the same mono mix written to both
     * slots - see the note on I2S_SLOT_MODE_STEREO in i2s_init(). */
    int16_t *samples = malloc(num_samples * 2 * sizeof(int16_t));
    if (samples == NULL) {
        ESP_LOGW(TAG, "startup chime: allocation failed, skipping");
        return;
    }

    for (int i = 0; i < num_samples; i++) {
        float t_ms = (float)i * 1000.0f / (float)sample_rate;

        /* Which note are we in, and how far into it? At most
         * CHIME_NOTE_COUNT (4) iterations, cheap per-sample. */
        const chime_note_t *note = &CHIME_NOTES[CHIME_NOTE_COUNT - 1];
        float local_ms = t_ms;
        float cursor_ms = 0.0f;
        for (size_t n = 0; n < CHIME_NOTE_COUNT; n++) {
            if (t_ms < cursor_ms + CHIME_NOTES[n].duration_ms) {
                note = &CHIME_NOTES[n];
                local_ms = t_ms - cursor_ms;
                break;
            }
            cursor_ms += CHIME_NOTES[n].duration_ms;
        }

        float envelope = chime_note_envelope(local_ms, note->duration_ms, note->release_ms);
        float sample = STARTUP_CHIME_AMPLITUDE * envelope *
                       sinf(STARTUP_CHIME_TWO_PI * note->freq_hz * (float)i / (float)sample_rate);

        int16_t s16 = (int16_t)sample;
        samples[2 * i] = s16;
        samples[2 * i + 1] = s16;
    }

    audio_pa_enable(true);
    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx_chan, samples, num_samples * 2 * sizeof(int16_t),
                                       &bytes_written, pdMS_TO_TICKS(3000));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "startup chime: i2s_channel_write failed (%s)", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(50)); /* let the amp settle before cutting power */
    audio_pa_enable(false);

    free(samples);
}

int16_t audio_measure_mic_level(void)
{
    const int sample_rate = HELIOS_AUDIO_SAMPLE_RATE_HZ;
    const int num_samples = sample_rate / 2; /* ~0.5s, per channel */

    /* Stereo-interleaved reads (see the note in i2s_init()) - peak is taken
     * over both slots below, whichever one actually carries the mic. */
    int16_t *samples = malloc(num_samples * 2 * sizeof(int16_t));
    if (samples == NULL) {
        ESP_LOGW(TAG, "mic level: allocation failed");
        return -1;
    }

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx_chan, samples, num_samples * 2 * sizeof(int16_t),
                                      &bytes_read, pdMS_TO_TICKS(1000));
    int16_t peak = 0;
    if (err == ESP_OK) {
        size_t n = bytes_read / sizeof(int16_t);
        for (size_t i = 0; i < n; i++) {
            /* Cast to int32_t before negating - int16_t can't represent
             * -INT16_MIN. */
            int16_t mag = (samples[i] < 0) ? (int16_t)(-(int32_t)samples[i]) : samples[i];
            if (mag > peak) {
                peak = mag;
            }
        }
    } else {
        ESP_LOGW(TAG, "mic level: i2s_channel_read failed (%s)", esp_err_to_name(err));
        peak = -1;
    }

    free(samples);
    return peak;
}
