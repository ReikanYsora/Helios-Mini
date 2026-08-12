#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Brings up the I2S bus, enables the PA, and initializes the ES8311 codec.
 * Audio is not a primary V1 feature (spec Section 5) - this exists to
 * validate the hardware path now and be ready for startup/notification
 * sounds later. Returns false if the codec init failed; I2S and the PA
 * enable line are still usable either way. i2c_bus_init() must have been
 * called first (the codec shares the touch I2C bus). */
bool audio_init(void);

void audio_pa_enable(bool enable);

/* Plays a synthesized rising-arpeggio "sunrise" chime (~0.8s, no audio
 * asset needed) to confirm the speaker path works and give the device an
 * actual startup sound rather than a debug beep. Notes play one at a
 * time, never stacked - see the comment in audio.c on why. Enables the PA
 * for the duration and disables it afterwards - blocks for roughly that
 * long. audio_init() must have succeeded first. */
void audio_play_startup_tone(void);

/* Records ~0.5s from the microphone and returns the peak sample magnitude
 * (0-32767), or -1 on error. Meant for a debug/self-test UI: talk or clap
 * close to the board while calling this and expect a value clearly above
 * the room's noise floor. audio_init() must have succeeded first. */
int16_t audio_measure_mic_level(void);
