# Helios Mini Firmware

ESP-IDF firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.32, targeting the
**V0.1 — Hardware Bring-Up** milestone from `docs/SPEC.md` (Section 34):
display, touch, Wi-Fi, speaker, microphone, buttons, USB. Pairing with Home
Assistant, the Helios UI, OTA, and diagnostics come in later versions.

## Status: V0.1, unbuilt

This has been written against Waveshare's own official example firmware for
this exact board (pin mapping, panel init sequence, I2S/audio pinout — see
[`docs/HARDWARE_REFERENCE.md`](../docs/HARDWARE_REFERENCE.md) for
provenance) but has **not yet been compiled or flashed** — no ESP-IDF
toolchain was available in the environment this was written in. Treat the
first `idf.py build` as part of finishing V0.1, not a formality. Known open
risks are called out in `docs/HARDWARE_REFERENCE.md` and in code comments
(search for "verify" / "unverified"), the main ones being:

- The `espressif/es8311` codec API surface (`firmware/hardware/audio`).
- Whether that API takes the new `i2c_master_bus_handle_t` (used here, to
  share the bus with touch) or the legacy `i2c_port_t`.
- The `espressif/button` API surface (`firmware/hardware/buttons`).
- Exact LVGL v9.x image/animation symbol names (`firmware/ui/animations`).
- The CO5300 init command sequence, ported from the SH8601-compatible driver
  Waveshare's own firmware uses (`firmware/hardware/display`).

## Requirements

- ESP-IDF **5.5.1** (matches Waveshare's own reference for this board)
- Target: `esp32s3`
- Python 3, as required by ESP-IDF's own `install.sh`

## Build

```sh
. $IDF_PATH/export.sh
cd firmware
idf.py set-target esp32s3
idf.py menuconfig   # under "Helios Mini", set a dev Wi-Fi SSID/password
                     # for bring-up (real provisioning is V0.2)
idf.py build
idf.py -p <port> flash monitor
```

The component manager will fetch `lvgl/lvgl` (^9), `espressif/esp_lcd_sh8601`,
`espressif/button`, and `espressif/es8311` on first build.

## Layout

Mirrors `docs/SPEC.md` Section 20, with one addition: a header-only
`hardware/board_config` component holds the single source of truth for
every GPIO pin, shared by all the other `hardware/*` components (instead of
each one duplicating pin numbers, or everything depending on `main`).

```
firmware/
├── main/                  app_main(): boot sequencing only
├── hardware/
│   ├── board_config/      pin map (header-only)
│   ├── power/              power-latch GPIO18 (must assert early, see HARDWARE_REFERENCE.md)
│   ├── i2c_bus/            shared I2C bus (touch + audio codec)
│   ├── touch/              CST820 driver
│   ├── display/            QSPI CO5300 bring-up + LVGL v9 port
│   ├── buttons/            BOOT/PWR via espressif/button
│   └── audio/              I2S + PA enable + ES8311
├── networking/wifi/       STA connect, NVS credentials + dev Kconfig fallback
├── storage/                NVS string get/set wrapper
├── diagnostics/            periodic heap/PSRAM/uptime log
└── ui/animations/          boot sequence (spec Section 15), Helios logo asset
```

`helios/`, `ota/`, and the rest of `ui/` (home/solar/battery/consumption/grid)
are intentionally still empty — V0.2/V0.3/V0.4 scope per the roadmap in
`docs/SPEC.md` Section 34.

## Regenerating the boot logo

The Helios logo used in the boot animation
(`firmware/ui/animations/assets/helios_logo.c`) is generated from
`assets/brand/helios-logo.svg` (pulled from the `Helios` repo) via:

```sh
python3 tools/asset-gen/svg_to_lvgl.py \
    assets/brand/helios-logo.svg 220 helios_logo \
    firmware/ui/animations/assets/helios_logo.c
```

Requires `rsvg-convert` (`brew install librsvg`) and Pillow. Do not hand-edit
the generated `.c` file.
