#pragma once

#include <stdbool.h>

/* Brings up the I2S bus, enables the PA, and initializes the ES8311 codec.
 * Audio is not a primary V1 feature (spec Section 5) - this exists to
 * validate the hardware path now and be ready for startup/notification
 * sounds later. Returns false if the codec init failed; I2S and the PA
 * enable line are still usable either way. i2c_bus_init() must have been
 * called first (the codec shares the touch I2C bus). */
bool audio_init(void);

void audio_pa_enable(bool enable);
