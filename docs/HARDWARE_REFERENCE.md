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

- ESP-IDF **5.5.1** (vendor's `sdkconfig.defaults` header comment; confirmed
  buildable with this exact version)
- Target: `esp32s3`
- Flash: 8 MB, QIO
- PSRAM: enabled, octal mode
- Managed components, as resolved and pinned in `firmware/dependencies.lock`:
  `lvgl/lvgl 9.5.0`, `espressif/esp_lcd_sh8601 2.0.1~1`,
  `espressif/button 4.2.0`, `espressif/es8311 1.0.0~1`

## Build-verified (2026-08-10)

`idf.py build` against ESP-IDF 5.5.1 succeeds with zero errors/warnings,
resolving `lvgl/lvgl 9.5.0`, `espressif/esp_lcd_sh8601 2.0.1~1`,
`espressif/button 4.2.0`, `espressif/es8311 1.0.0~1` (pinned in
`firmware/dependencies.lock`, committed for reproducibility). Two guessed
API surfaces turned out wrong and are now fixed in code; see
`firmware/README.md` for the summary. Notably:

- **`espressif/es8311`'s `es8311_create()` takes the legacy `i2c_port_t`**,
  not a `i2c_master_bus_handle_t`. Because the codec shares its physical
  I2C bus/pins with the touch controller, and a port can only be owned by
  one driver generation at a time, `hardware/i2c_bus` and `hardware/touch`
  were both moved to the legacy `driver/i2c.h` API to match. This is the
  resolution to the "shared bus" open question below.
- **`espressif/button`'s real entry point is `iot_button_new_gpio_device()`**
  (`button_config_t` only holds press-timing fields; GPIO config is a
  separate `button_gpio_config_t` passed alongside it), and
  `iot_button_register_cb()` takes 5 args including a
  `button_event_args_t *` (pass `NULL` for click/long-press events).
- `LV_IMAGE_DECLARE`, `lv_image_create`, `lv_obj_set_style_image_opa` (LVGL
  v9 API used in `ui/animations`) compiled as written against 9.5.0.
- `ES8311_ADDRRES_0` (the codec I2C address constant, typo and all) is
  real, defined in `es8311.h` for backward compatibility.

## Wi-Fi provisioning (added 2026-08-10)

`networking/provisioning` runs the ESP32-S3 in **`WIFI_MODE_APSTA`**
(AP + STA concurrently) so it can host the setup network *and* run a
blocking `esp_wifi_scan_start()` on the STA side to list nearby networks
in the setup page - both interfaces are backed by the built-in Wi-Fi radio
and this concurrent mode is a standard, documented ESP-IDF capability, not
something specific to this board.

The setup AP (`HELIOS-MINI-XXXX`) is **open, no password**, deliberately -
it only exists for the short window between unboxing and Wi-Fi setup, on a
device with no other sensitive state yet. Revisit before commercial
shipment: a per-unit default password (printed on the device/box) would be
a low-cost hardening step once V0.4 packaging/production is in scope (spec
Section 23).

## First hardware boot (2026-08-11)

First real flash hit a task watchdog timeout / crash loop, backtrace
bottoming out in `lv_inv_area` called from `boot_animation_start()`. Root
cause: `boot_animation_start()` called `lv_obj_*`/`lv_anim_*` directly from
the `main` task without taking the LVGL lock, while `hardware/display`'s own
`lvgl` task was concurrently running `lv_timer_handler()` in a loop. LVGL is
not thread-safe - two tasks mutating the object tree/invalidated-area
bookkeeping at the same time corrupted it, which showed up as an infinite
loop inside `lv_inv_area` and tripped the watchdog. `networking/provisioning`
already followed the `display_lock()`/`display_unlock()` contract from
`display.h` correctly; `boot_animation_start()` didn't. Fixed by wrapping
its whole body in `display_lock(0)`/`display_unlock()` (the animation
callbacks themselves - `dot_grow_cb`, `arc_sweep_cb`, `arc_sweep_done_cb`,
`logo_fade_cb` - don't need their own lock, since LVGL invokes them from
inside `lv_timer_handler()`, i.e. already inside the `lvgl` task's lock).
Confirmed fixed on reflash: boot animation runs, no crash.

Second crash, hit as soon as a phone actually joined the setup AP and the
`httpd` task did real work: `A stack overflow in task httpd has been
detected`. Cause: `root_get_handler()` kept a `wifi_ap_record_t aps[20]`
(~1.9KB) plus HTML-rendering buffers (~700B) on the stack, against
`esp_http_server`'s default 4KB task stack - not enough headroom once the
server's own request/header parsing is added on top. Fixed by
heap-allocating the scan-results array (`calloc`/`free`) and bumping
`httpd_config_t.stack_size` to 8192. Confirmed fixed: a phone connected to
the AP, loaded the setup page (network scan included), submitted real
credentials (`TITEFAMILLE`), and the device saved them, rebooted, and
connected in station mode — full round trip, no crash, got a real DHCP
lease (`192.168.0.45`) on the target network. This is the first fully
successful end-to-end run of V0.1 + the V0.2 Wi-Fi-provisioning slice.

## Still open (need the physical board)

- [ ] Confirm the CO5300 init command sequence actually produces a correct
      image (ported from the vendor's SH8601-driver init array — compiles
      fine, works in practice per the vendor's own shipped example, but not
      independently verified against a CO5300 register reference or seen
      on an actual panel yet).
- [ ] Confirm `TE` (GPIO 9) is safe to leave unconnected in software for V0.1
      (tearing may be visible without it; acceptable for bring-up, revisit
      for V0.3 UI polish).
- [ ] Confirm touch coordinate mirroring (`hardware/display`'s
      `touch_read_cb`) is correct for this panel's actual mounting
      orientation - ported from the vendor example but easy to get
      backwards; verify by touching the four edges once flashed.
- [ ] Confirm the power-latch behavior (GPIO 18) end-to-end in the
      USB-only configuration: does the board actually stay powered after
      the PWR button is released once V0.1 asserts the latch, exactly as
      it does in the vendor's battery-equipped reference?
