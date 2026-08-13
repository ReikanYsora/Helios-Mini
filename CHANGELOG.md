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
- `helios/ha_ws`: a Home Assistant WebSocket client
  (`espressif/esp_websocket_client`) - authenticates with the stored
  Long-Lived Access Token, then auto-discovers each ring's entity from
  Home Assistant's own Energy Dashboard configuration (Settings →
  Dashboards → Energy) and subscribes to live updates. Zero manual entity
  entry, and power only: only each source's linked live power (W) entity
  is ever used (`stat_rate` / `power_config.stat_rate_from`/`stat_rate_to`
  - the same entities the parent Helios HA card's own power chips read).
  The cumulative energy (kWh) statistic Home Assistant always keeps
  alongside it for billing/history is never touched, not even as a
  fallback - a ring with no power entity linked is simply not configured.
  A ring can be fed by more than one power entity (e.g. multiple solar
  strings) - every one found is subscribed and summed.
- `helios/energy_model`: turns `helios/ha_ws`'s live status into ring
  fill/color every 3s (grid import vs. export, battery charge vs.
  discharge, whichever direction is currently active), against
  configurable ring limits, and drives `ui/home`. Switches the screen
  from the persistent IP notice to the rings the first time the Energy
  Dashboard has any source configured. Entity/color model mirrors the
  parent Helios HA card's `chip-appearance.ts` exactly, including battery
  being two separate entities, not one signed value.
- `/display`, `/display/save`, `/display/rescan` on the settings server:
  a read-only auto-discovery status panel (which power entity Home
  Assistant reported for each ring, live value, staleness, or why not -
  not set up in the Energy Dashboard / Home Assistant not set up / not
  tested yet) plus the ring-limits form and a manual rescan trigger for
  after the user edits their Energy Dashboard in HA. New "Display"
  sidebar item (between Home Assistant and Debug).
- Number format preference (`energy_config.h`'s `energy_format_t`, NVS
  backed): W or kW, 0-3 decimal places, default W/1 decimal. One shared
  `energy_format_power()` formatter used by both the rings' center text
  and `/display`'s status panel. New unit `<select>` + decimals
  `<input type=range>` on `/display`'s existing limits form.
- Rings now animate into place (a real `lv_anim_t` lerp, not a hard
  jump), have no background track (only the colored fill draws, with a
  small rounded-cap dot always marking a ring's start/end even at
  0%/not-configured), and use rounded Apple-Watch-style caps throughout.
  A small MDI house icon sits above the center number instead of a "home
  consumption" text label. Home consumption itself now mirrors the
  parent Helios card's own formula verbatim (`consumptionLoad()` in
  `helios/src/core/energy.ts`): `production + gridImport - gridExport -
  netBattery`, clamped at 0 - the previous formula was an unverified
  guess missing the battery term.
- Boot animation redesigned: the Helios logo draws itself in, its 12
  sun-flame rays fading in one by one clockwise starting from the topmost
  ray, then the central disc - instead of a generic dot+arc spinner. New
  `tools/asset-gen/svg_pieces_to_lvgl.py` splits a multi-subpath SVG (the
  logo happens to already be 13 separate "M...Z" shapes: 12 rays + the
  disc) into individually cropped LVGL image assets, smaller in flash
  altogether than the one flattened image it replaces.
- The IP notice is skipped at boot entirely when Home Assistant is
  already configured, rather than always flashing on screen for a few
  seconds before the energy rings replace it.

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
  dynamic value being rendered was an empty string (e.g. `/ha` before a
  URL is saved): `httpd_resp_sendstr_chunk(req, "")` hits the exact same
  "zero-length buffer ends the chunked response" signal `esp_http_server`
  uses on purpose for the real end-of-page terminator. Added a
  `send_chunk()` wrapper that skips empty/NULL strings (a no-op for the
  HTML either way) and routed every dynamic string in `settings_server.c`
  through it, keeping the one deliberate `NULL` terminator in
  `close_page()` as a direct call. See `docs/HARDWARE_REFERENCE.md`.
- Home Assistant's Energy Dashboard `energy/get_prefs` grid schema didn't
  match the API docs on the user's real instance (flat
  `stat_energy_from`/`stat_energy_to` and multiple `"grid"` entries,
  not the documented `flow_from`/`flow_to` arrays) - `helios/ha_ws`
  silently found nothing until the parser was rewritten against the real,
  logged response. See `docs/HARDWARE_REFERENCE.md`.
- Two stack overflows in `helios/ha_ws`/`helios/energy_model` once
  summing multiple sources per ring made their local/struct sizes grow -
  same class of bug as the `wifi_ap_record_t` one below. Fixed by
  heap-allocating the discovery scratch buffer and sizing both tasks'
  stacks with real headroom. See `docs/HARDWARE_REFERENCE.md`.
- Home consumption (center text) was computed from an independent guess
  (`solar + grid_import - grid_export`) instead of the parent Helios
  card's own formula, missing the battery term entirely. Now mirrors
  `consumptionLoad()` from `helios/src/core/energy.ts` verbatim: `load =
  production + gridImport - gridExport - netBattery` (`netBattery =
  charge - discharge`), clamped at 0.
- `ui/home`'s rings had a stray dot at the indicator's end (`lv_arc`'s
  default draggable "knob", not fully suppressed by
  `lv_obj_remove_style()` alone) and square-cut caps instead of the
  intended Apple Watch look. Knob fully suppressed via explicit
  transparent/zero-width overrides on every style property that could
  paint it; both ring parts switched to rounded caps.
- The old boot animation's "central light" dot and its circular sweep arc
  were created and animated but never deleted or faded out once the logo
  faded in over them - both lingered, a small amber dot dead center of
  the screen, for as long as the boot scene stayed up. Moot now that the
  whole scene was replaced (see Added), but the same "animate it, then
  forget to clean it up" bug is worth remembering for next time.
- Power-on white flash: `hardware/display`'s CO5300 init command table
  set brightness to `0xFF` as its *sixth* command, sent well before Sleep
  Out/Display On even ran - the panel showed whatever garbage was already
  in GRAM at full brightness for the whole rest of boot. A first attempt
  (ramp brightness up only after the first real LVGL frame flushed, via a
  new `s_first_flush_done` flag set from the panel IO's
  `on_color_trans_done` ISR) didn't fully fix it, because it didn't
  address that early `0xFF` in the init table itself. Fixed at the actual
  source: that entry is now `0x00`, so brightness starts at 0 before the
  panel is ever unblanked and only the `s_first_flush_done` mechanism
  ramps it up, once real content is confirmably on screen.

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
`helios/ha_ws` connected to the user's real Home Assistant instance,
authenticated, and auto-discovered their actual Energy Dashboard
configuration with zero manual entry: solar, grid import, and grid
export all went live within seconds showing real wattages from genuine
power entities (a Shelly Pro EM50), correctly ignoring the separate Linky
energy-only meters also present in their config. `/display/rescan`
exercised live. No crashes or reboots in the serial log throughout,
including through two stack-overflow fixes along the way. Not yet
verified: the rings actually rendering correctly on the physical round
panel, or a battery source (none configured on the test instance) - see
`docs/HARDWARE_REFERENCE.md`'s "Still open" list.
