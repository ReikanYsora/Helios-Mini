# Hardware Reference — Waveshare ESP32-S3-Touch-AMOLED-1.32

This document records the verified GPIO pin mapping and low-level bring-up
notes for the Waveshare ESP32-S3-Touch-AMOLED-1.32 board used by Helios Mini
V0.1–V0.4.

**Source of truth:** the vendor's own official example firmware,
[`waveshareteam/ESP32-S3-Touch-AMOLED-1.32`](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.32)
(`Example/ESP-IDF/`), which is the only place Waveshare publishes the actual
pin assignments — the product documentation pages do not list them.

Per spec §42, these must be revalidated against the exact production
revision before manufacturing / before ordering a batch.

## SPI bus — display (CO5300, QSPI)

The AMOLED panel is driven over QSPI. Waveshare's own factory/example
firmware brings it up with the `esp_lcd_sh8601` component (the CO5300's
command set is SH8601-compatible in practice) rather than a dedicated
`esp_lcd_co5300` driver.

| Signal        | GPIO | Notes                                  |
|---------------|------|-----------------------------------------|
| SPI host      | —    | `SPI2_HOST`                             |
| CS            | 10   |                                          |
| SCLK          | 11   |                                          |
| D0            | 12   | QSPI data 0                             |
| D1            | 13   | QSPI data 1                             |
| D2            | 14   | QSPI data 2                             |
| D3            | 15   | QSPI data 3                             |
| RST           | 8    |                                          |
| TE             | 9    | Tearing-effect signal (not read by the vendor example; reserved) |
| Backlight     | —    | No PWM GPIO; brightness is set via a QSPI command (`0x51`) to the panel controller, not a GPIO. |

Panel: 466×466, RGB565, `esp_lcd_panel_io_spi` in quad mode, 40 MHz pixel
clock, 32-bit LCD command width / 8-bit parameter width (QSPI addressing
quirk of this controller family, not a typo).

## I2C bus — touch + audio codec (shared)

Touch (CST820) and the audio codec (ES8311) sit on the **same** I2C bus.

| Signal | GPIO |
|--------|------|
| SDA    | 47   |
| SCL    | 48   |
| Port   | `I2C_NUM_0` |
| Speed  | 300 kHz (touch); confirm codec can run at the same bus speed before sharing a single init |

### Touch — CST820

| Signal    | GPIO | Notes |
|-----------|------|-------|
| I2C addr  | 0x15 | 7-bit |
| RST       | 7    | Active-low reset pulse |
| INT       | 6    | Not used for interrupt-driven reads in the vendor example (polled instead); wired but optional for V0.1 |

Gesture/position registers read directly (no `esp_lcd_touch` framework used
by the vendor): gesture count at `0x02` (2 bytes), position at `0x03`
(4 bytes) — `x = (Gpos[0] & 0x0F) << 8 | Gpos[1]`, `y = (Gpos[2] & 0x0F) << 8 | Gpos[3]`.

## Audio — ES8311 codec + speaker

From the vendor's `codec_board` board profile for `S3_AMOLED_1_32`:

| Signal | GPIO |
|--------|------|
| I2S MCLK | 38 |
| I2S BCLK | 39 |
| I2S WS (LRCK) | 41 |
| I2S DIN (codec → ESP, mic path) | 40 |
| I2S DOUT (ESP → codec, speaker path) | 42 |
| PA enable | 46 |
| Codec I2C addr | shares the touch I2C bus (SDA 47 / SCL 48) |

`use_mclk: 1` — the codec needs the MCLK line driven (not MCLK-less mode).

## Buttons

| Button | GPIO | Active level |
|--------|------|---------------|
| BOOT   | 0    | Low (standard ESP32 strapping pin; also used for download-mode entry) |
| PWR    | 17   | Low |

## Power latch

| Signal | GPIO | Notes |
|--------|------|-------|
| Power hold / latch enable | 18 | Must be driven **HIGH early in boot** and held there. The board's power path is a physical PWR-button + latch circuit: a press turns the rail on, the MCU boots, and firmware must assert this pin to keep the rail powered — otherwise the board powers back off once the button is released. Confirmed present and asserted in the vendor's own examples even though Helios Mini ships without a battery; must be re-verified on hardware in the USB-only configuration (the vendor's own reference always asserts it regardless of battery presence, so we do the same defensively). |

Battery ADC (`ADC_CHANNEL_3`) exists on the board but is **not used** by
Helios Mini — no battery in the commercial configuration (spec §4.1).

## ESP-IDF / component versions

- ESP-IDF **5.5.1** (vendor's `sdkconfig.defaults` header comment)
- Target: `esp32s3`
- Flash: 8 MB, QIO
- PSRAM: enabled, octal mode
- LVGL: version `^9` (`lvgl/lvgl` managed component)
- Display driver: `esp_lcd_sh8601` managed component (Espressif Component Registry)

## Open questions for hardware bring-up

- [ ] Confirm CO5300 init command sequence against the actual CO5300
      datasheet (currently ported from the vendor's SH8601-driver init array
      — works in practice per the vendor's own shipped example, but not
      independently verified against a CO5300 register reference).
- [ ] Confirm `TE` (GPIO 9) is safe to leave unconnected in software for V0.1
      (tearing may be visible without it; acceptable for bring-up, revisit
      for V0.3 UI polish).
- [ ] Confirm exact `espressif/es8311` component API surface against the
      version actually resolved by `idf.py` at first build — the codec
      register-level init in `hardware/audio` is a thin wrapper around it
      and has not been compiled/tested against real silicon yet.
- [ ] Confirm `es8311_create()` accepts the new `i2c_master_bus_handle_t`
      (what `hardware/audio` passes it, to share the bus with `hardware/touch`)
      rather than the legacy `i2c_port_t`. If the resolved component version
      only supports the legacy I2C driver, it cannot share this bus as-is —
      the touch driver would need to move to the legacy driver too, or the
      codec would need its own bus instance (not possible on shared pins
      without a second bus).
- [ ] Confirm `espressif/button` (`hardware/buttons`) API surface the same
      way — `iot_button_create()` / `iot_button_register_cb()` signatures
      have not been compiled against the resolved version either.
- [ ] Confirm LVGL v9.x image/animation symbol names used in
      `ui/animations` (`LV_IMAGE_DECLARE`, `lv_image_create`,
      `lv_obj_set_style_image_opa`) against the resolved `lvgl/lvgl`
      component version.
