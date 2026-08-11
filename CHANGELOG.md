# Changelog

All notable changes to Helios Mini will be documented in this file.

## [Unreleased]

### Added

- Initial repository structure (firmware, home-assistant, hardware, enclosure, installer, assets, docs, tools).
- Product specification (`docs/SPEC.md`).
- Verified GPIO pin mapping and bring-up notes for the Waveshare
  ESP32-S3-Touch-AMOLED-1.32, sourced from Waveshare's own official example
  firmware (`docs/HARDWARE_REFERENCE.md`).
- Helios brand logo asset (`assets/brand/helios-logo.svg`, from the `Helios`
  repo) and an SVG-to-LVGL-v9 C image generator (`tools/asset-gen/svg_to_lvgl.py`).
- V0.1 Hardware Bring-Up firmware (ESP-IDF): power latch, shared I2C bus,
  CST820 touch, QSPI CO5300 display + LVGL v9 port, BOOT/PWR buttons,
  I2S + ES8311 audio bring-up, Wi-Fi station connect, NVS storage wrapper,
  diagnostics logging, and the Section 15 boot animation using the Helios
  logo. See `firmware/README.md` for the day-of build/flash checklist.
- Builds clean (zero errors/warnings) against ESP-IDF 5.5.1, with
  `firmware/dependencies.lock` committed to pin the exact managed-component
  versions verified to compile: `lvgl/lvgl 9.5.0`,
  `espressif/esp_lcd_sh8601 2.0.1~1`, `espressif/button 4.2.0`,
  `espressif/es8311 1.0.0~1`. Fixed two guessed API surfaces that turned out
  wrong (`espressif/es8311` needs the legacy I2C driver, so the shared
  touch/audio I2C bus moved to it too; `espressif/button`'s real entry
  point is `iot_button_new_gpio_device()`) — see
  `docs/HARDWARE_REFERENCE.md`.
- Wi-Fi provisioning (`firmware/networking/provisioning`), pulled forward
  from V0.2 (spec Section 16): on first boot the device starts an open
  SoftAP `HELIOS-MINI-XXXX` + a plain HTTP server at `http://192.168.4.1/`
  where the user scans/picks a Wi-Fi network and enters its password, no
  app or JavaScript required. Credentials are saved to NVS and the device
  reboots into station mode. Holding BOOT at power-on forces re-entry into
  setup.
- Persistent on-screen network status (`ui/animations/network_status.c`):
  once station mode gets an IP, the screen shows "Helios Mini /
  `http://<ip>/`" and keeps it current across reconnects, via a new
  `wifi_sta_set_connected_cb()`.
- Home Assistant settings server (`networking/settings_server`), replacing
  the discovery/pairing flow originally sketched in spec Sections 17-18: a
  small HTTP server on the station IP where the user pastes an HA base URL
  and a Long-Lived Access Token, saved to NVS. Only captures the settings
  so far - a WebSocket client that uses them to pull energy data doesn't
  exist yet.
- `networking/http_forms`: shared URL-decode/form-parsing/HTML-escape
  helpers, factored out of `provisioning` and reused by `settings_server`.
- Startup chime (`hardware/audio`'s `audio_play_startup_tone()`): a
  synthesized tone with a fade envelope, written straight to the I2S TX
  channel - no audio asset needed - to confirm the speaker path works.
- Doubled the boot logo (220px -> 440px).

### Fixed

- Task-watchdog crash loop on first real hardware flash: `boot_animation_start()`
  called LVGL directly from the `main` task without the display component's
  lock, racing `hardware/display`'s own `lvgl` task and corrupting LVGL's
  internal state (hung inside `lv_inv_area`). Wrapped in
  `display_lock()`/`display_unlock()`. See `docs/HARDWARE_REFERENCE.md`.
- Stack overflow in the `httpd` task, hit as soon as a phone actually
  connected to the setup portal: the Wi-Fi scan-results buffer
  (`wifi_ap_record_t aps[20]`, ~1.9KB) plus HTML-rendering buffers lived on
  the stack against the server's default 4KB task stack. Heap-allocated the
  scan buffer and bumped `httpd_config_t.stack_size` to 8192.
- Panel was upside down: `hardware/display` never sent a MADCTL (0x36)
  orientation command. Added `MADCTL = 0xC0`, matching Waveshare's own
  factory firmware for this board.

### Verified on hardware (2026-08-11)

First flash on the real board (both crashes above hit and fixed along the
way). After fixing them, a full live run succeeded end-to-end: booted,
started the `HELIOS-MINI-XXXX` setup AP, a phone joined it, loaded the
setup page (network scan included), submitted real Wi-Fi credentials,
the device saved them to NVS, rebooted, connected in station mode, and
got a DHCP lease on the target network. V0.1 hardware bring-up + the V0.2
Wi-Fi-provisioning slice are now confirmed working, not just compiling.
See `docs/HARDWARE_REFERENCE.md`.
