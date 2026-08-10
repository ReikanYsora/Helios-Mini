#pragma once

/* Single source of truth for the Waveshare ESP32-S3-Touch-AMOLED-1.32 pin
 * mapping used by every hardware component. Values are sourced from
 * Waveshare's own official example firmware, not the product docs (which
 * don't list pins) - see docs/HARDWARE_REFERENCE.md for provenance and open
 * questions, and revalidate against the exact production revision before
 * manufacturing (spec Section 42). */

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"

/* The shared I2C bus (below) uses the legacy driver/i2c.h API rather than
 * the new driver/i2c_master.h: the espressif/es8311 codec component only
 * supports the legacy driver (i2c_master_write_to_device() internally), and
 * a bus/port can only be owned by one driver generation at a time - so
 * hardware/touch uses the legacy API too, to share the port. See
 * docs/HARDWARE_REFERENCE.md. */

/* ---- Display (QSPI, CO5300 panel via SH8601-compatible driver) ---- */
#define HELIOS_LCD_HOST         SPI2_HOST
#define HELIOS_LCD_H_RES        466
#define HELIOS_LCD_V_RES        466
#define HELIOS_PIN_LCD_CS       10
#define HELIOS_PIN_LCD_SCLK     11
#define HELIOS_PIN_LCD_DATA0    12
#define HELIOS_PIN_LCD_DATA1    13
#define HELIOS_PIN_LCD_DATA2    14
#define HELIOS_PIN_LCD_DATA3    15
#define HELIOS_PIN_LCD_RST      8
#define HELIOS_PIN_LCD_TE       9   /* wired; not read by software yet (V0.1) */

/* ---- Shared I2C bus: touch (CST820) + audio codec (ES8311) ---- */
#define HELIOS_I2C_PORT         I2C_NUM_0
#define HELIOS_PIN_I2C_SDA      47
#define HELIOS_PIN_I2C_SCL      48

/* ---- Touch (CST820) ---- */
#define HELIOS_TOUCH_I2C_ADDR   0x15
#define HELIOS_PIN_TOUCH_RST    7
#define HELIOS_PIN_TOUCH_INT    6  /* wired; polled for now, not interrupt-driven */

/* ---- Buttons ---- */
#define HELIOS_PIN_BUTTON_BOOT  0
#define HELIOS_PIN_BUTTON_PWR   17

/* ---- Power latch (must be asserted early in app_main(), see hardware/power) ---- */
#define HELIOS_PIN_POWER_LATCH  18

/* ---- Audio (I2S + ES8311 codec + PA) ---- */
#define HELIOS_PIN_I2S_MCLK     38
#define HELIOS_PIN_I2S_BCLK     39
#define HELIOS_PIN_I2S_WS       41
#define HELIOS_PIN_I2S_DIN      40
#define HELIOS_PIN_I2S_DOUT     42
#define HELIOS_PIN_AUDIO_PA_EN  46
