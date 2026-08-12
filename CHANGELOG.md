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
  synthesized sound, written straight to the I2S TX channel - no audio
  asset needed - to confirm the speaker path works and give the device an
  actual product sound. Went through two redesigns based on listening to
  the real hardware - see "Fixed" below.
- Doubled the boot logo (220px -> 440px).
- Home Assistant mDNS discovery (`networking/ha_discovery`): searches for
  `_home-assistant._tcp.local.` on the LAN using the `espressif/mdns`
  managed component, exposed at `/scan` on the settings server. Picking a
  result prefills the URL field; Save still has to be pressed.
- Home Assistant token validation (`helios/ha_client`): a REST call to
  `<url>/api/` with the token as a Bearer credential, run right after
  Save and from a "Test connection again" link. The entire HA client for
  now - no WebSocket, no energy data. Home Assistant Long-Lived Access
  Tokens don't carry an expiry date the API exposes, so there's nothing to
  proactively track; this detects a rejected token (401/403) whenever it's
  used and tells the user to generate a fresh one instead.
- Hardware debug page (`/debug` on the settings server, backed by new
  `diagnostics_get_status()`/`diagnostics_test_*()` functions): live
  uptime/heap/PSRAM/Wi-Fi-RSSI status, plus buttons to flash the screen,
  play the startup chime, and measure the microphone's peak level. No
  gyroscope test - this board doesn't have one (confirmed against
  Waveshare's own example repo for this exact board).
- Settings app redesign (`networking/settings_server`): a topbar (Helios
  logo + live Wi-Fi/Home Assistant status icons) and a sidebar (Network /
  Home Assistant / Debug, collapses to icons-only on narrow screens) frame
  every page now. Icons are real MDI (Material Design Icons - the same set
  Home Assistant's own frontend uses), extracted from the user's local
  `ha-frontend` checkout's `@mdi/js` package and embedded inline as SVG
  (`firmware/networking/settings_server/mdi_icons.h`) - no external
  requests, no icon font.
- Routes reorganized: `/` -> `/network` (Wi-Fi status, new setup-AP
  toggle), the old root form -> `/ha` (+ `/ha/save`, `/ha/test`,
  `/ha/scan`), `/debug` unchanged in substance.
- Setup-AP toggle on `/network` (`wifi_sta_set_setup_ap_enabled()`):
  switches `WIFI_MODE_STA` <-> `WIFI_MODE_APSTA` at runtime, letting a
  second device join `HELIOS-MINI-XXXX` and reach the same settings app at
  `http://192.168.4.1/` without disturbing the existing station connection
  - the already-running `settings_server` becomes reachable there too
  automatically, since `esp_http_server` binds to all interfaces.
- Energy rings (`ui/home`): three concentric Apple-Watch-style `lv_arc`
  rings - solar production, grid import/export, battery charge/discharge -
  plus the home's current consumption as big text in the middle. A pure
  LVGL renderer with no Home Assistant knowledge of its own.
- `helios/energy_model`: polls Home Assistant every 10s for the 6
  configured entities, decides which direction is live for the two
  two-entity rings (grid import vs. export, battery charge vs. discharge),
  computes fill percentages against configurable limits, and drives
  `ui/home`. Switches the screen from the persistent IP notice to the
  rings the first time a "home" entity is configured. Entity/color model
  mirrors the parent Helios HA card's `chip-appearance.ts` exactly,
  including battery being two separate entities, not one signed value.
- `helios/ha_client` gained `ha_client_get_entity_power()`: fetches one
  entity's numeric state via cJSON, auto-converting `kW`/`MW` to watts,
  and reports one of six outcomes (OK / not configured / not found /
  unavailable / not numeric / unauthorized / unreachable) so nothing is
  ever silently blank.
- `/display` + `/display/save` on the settings server: which entity feeds
  each ring, each ring's 100% power reference, and a live per-entity
  status panel using every `ha_entity_status_t` outcome above. Saving
  tests every filled-in entity immediately against the real Home
  Assistant instance, same as `/ha/save`. New "Display" sidebar item
  (between Home Assistant and Debug).

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
- Startup chime was silent: I2S was configured in mono slot mode, which
  the ES8311 (like most cheap I2S codecs) doesn't handle - switched to
  stereo with the mono signal duplicated onto both L/R slots.
- Startup chime then sounded like a ship's foghorn: it played four notes
  as a simultaneous chord, and the beat frequencies between close-together
  tones on a small single-driver speaker land right in "foghorn wah-wah"
  territory. Rewritten as a sequential rising arpeggio - one note at a
  time, nothing left to beat against.
- Settings-server pages silently truncated mid-response whenever a
  dynamic value being rendered was an empty string (e.g. `/display` with
  no entities configured yet, or `/ha` before a URL is saved):
  `httpd_resp_sendstr_chunk(req, "")` hits the exact same "zero-length
  buffer ends the chunked response" signal `esp_http_server` uses on
  purpose for the real end-of-page terminator. Added a `send_chunk()`
  wrapper that skips empty/NULL strings (a no-op for the HTML either way)
  and routed every dynamic string in `settings_server.c` through it,
  keeping the one deliberate `NULL` terminator in `close_page()` as a
  direct call. See `docs/HARDWARE_REFERENCE.md`.

### Changed

- Automatic startup chime disabled on boot: the rising-arpeggio redesign
  was still judged "absolutely awful" by the user. `audio_play_startup_tone()`
  is unchanged and still reachable manually from `/debug/speaker` for
  whenever the sound design resumes deliberately instead of iterating
  blind between flashes.

### Verified on hardware (2026-08-11 - 2026-08-12)

First flash on the real board (both crashes above hit and fixed along the
way). After fixing them, a full live run succeeded end-to-end: booted,
started the `HELIOS-MINI-XXXX` setup AP, a phone joined it, loaded the
setup page (network scan included), submitted real Wi-Fi credentials,
the device saved them to NVS, rebooted, connected in station mode, and
got a DHCP lease on the target network. V0.1 hardware bring-up + the V0.2
Wi-Fi-provisioning slice are now confirmed working, not just compiling.
The redesigned settings app's `/network`, `/ha`, and `/debug` pages were
fetched over the LAN with `curl` (200s, well-formed HTML) and the new
setup-AP toggle was exercised end-to-end on the running device without
losing the station connection. See `docs/HARDWARE_REFERENCE.md`.

### Verified on hardware (2026-08-13)

`/display`, `/ha`, `/network`, `/debug` all fetched over the LAN with
`curl`, each response checked to actually end in `</html>` (the empty-
string chunk-truncation bug above was found and fixed in this pass).
`/display/save` exercised twice against the real board: once with a
deliberately wrong entity id, which correctly came back "no such entity
in Home Assistant" - a real 404 round-tripped end-to-end from the user's
actual Home Assistant instance, confirming auth, the REST call, and the
JSON/status-code handling all work - then again with everything cleared
back to empty. No crashes or reboots in the serial log throughout. Not
yet verified: the rings actually rendering correctly on the physical
round panel, or tracking real entities over time (see
`docs/HARDWARE_REFERENCE.md`'s "Still open" list).
