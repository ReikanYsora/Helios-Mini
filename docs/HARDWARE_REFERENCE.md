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

## Display orientation, audio, and HA-settings changes (2026-08-11)

Feedback from looking at the flashed device on the desk:

- **Panel was upside down.** `hardware/display`'s `panel_init()` never sent
  a MADCTL (0x36) orientation command at all - the panel was just running
  on its raw controller default. Waveshare's own factory firmware sends
  `MADCTL = 0xC0` (MY=1, MX=1) for this exact board; added the same command.
  Since `touch_read_cb`'s coordinate mirroring was ported from the same
  vendor reference (which also sends `0xC0`), the two should now be
  consistent with each other again rather than one being un-rotated. Touch
  itself is still unverified either way (see below).
- **Boot logo doubled** (220px -> 440px) via
  `tools/asset-gen/svg_to_lvgl.py ... 440 ...`.
- **Startup tone added** (`hardware/audio`'s `audio_play_startup_tone()`):
  a synthesized 880Hz chime with a fade envelope, no audio asset needed,
  written straight to the I2S TX channel. Confirms the speaker path
  end-to-end. Not yet confirmed audible - needs eyes/ears on the device,
  not just a clean serial log.
- **Persistent on-screen IP** (`ui/animations/network_status.c`): once
  station mode gets an IP, the screen switches to "Helios Mini /
  `http://<ip>/`" and stays there - wired via a new `wifi_sta_set_connected_cb()`
  so it also updates itself across reconnects/drops.
- **Home Assistant discovery/pairing (spec Sections 17-18) dropped**, in
  favor of a much simpler mechanism: `networking/settings_server` runs a
  small HTTP server on the station IP (the same one now shown on screen)
  where the user pastes an HA base URL and a Long-Lived Access Token,
  saved to NVS. This only covers *capturing* the settings - a WebSocket
  client that actually uses the token to pull energy data does not exist
  yet, that's the next real chunk of work.
- Extracted the URL-decode/form-parsing/HTML-escape helpers that
  `networking/provisioning` had into a shared `networking/http_forms`
  component, now used by both `provisioning` and `settings_server`, so the
  stack-overflow-class bug above only has one place to be wrong in.

Verified: builds clean, flashes, boots without crashing, reconnects to the
already-provisioned network automatically, `settings_server` is reachable
(a browser already hit it and got a normal 404 for `/favicon.ico`). Visual
confirmation of the rotation fix and logo size, and audible confirmation of
the startup tone, are pending - need the user looking at/listening to the
actual device.

## Home Assistant discovery, token validation, debug page (2026-08-11)

Three more pieces added on top of `settings_server`:

- **`networking/ha_discovery`**: mDNS browse for `_home-assistant._tcp.local.`
  (the service Home Assistant's own Zeroconf integration advertises under),
  using the `espressif/mdns` managed component. Exposed at `/scan` on the
  settings server; picking a result prefills the URL field but still
  requires pressing Save (no silent auto-connect). The `mdns_result_t`
  field names used in `ha_discovery.c` were guesses when written (same
  situation as the es8311/button surprises earlier) but **compiled clean
  on the first try** against the resolved `espressif/mdns 1.11.3` - no
  fixes needed here, unlike those two.
- **`helios/ha_client`**: a REST call to `<url>/api/` with the token as a
  Bearer credential, used both right after Save and from a "Test
  connection again" link. This is the *entire* extent of the Home
  Assistant client for now — no WebSocket, no energy data. On "token
  expiration": Home Assistant Long-Lived Access Tokens don't carry an
  expiry date the API exposes (they're valid until revoked), so there's
  nothing to proactively track. What this does instead: detect a rejected
  token (401/403) whenever it's actually used, and tell the user to
  generate a fresh one - the only thing actually possible here.
- **`/debug`** on the settings server: a live system status line
  (`diagnostics_get_status()`) plus three hardware self-tests
  (`diagnostics_test_screen/speaker/microphone()`) - flash a few colors,
  play the startup tone, report peak microphone level. No gyroscope test:
  **this board doesn't have one.** Confirmed by checking Waveshare's own
  example repo for this exact board (`ESP32-S3-Touch-AMOLED-1.32`) - no
  IMU/gyro example anywhere in it, unlike some other Waveshare AMOLED
  variants (e.g. the 1.8" one) whose product listings do advertise one.
  Said so explicitly on the debug page rather than omitting it silently.

**Verified live on hardware, by the user testing through a browser while
this was being watched over serial** (not just a clean log - actual
functional confirmation): saved a real Home Assistant URL + token through
`/save`, and `ha_client_test_connection()` returned `ok` against the real
instance. mDNS (`mdns_mem: mDNS task will be created...`) started without
crashing when `/scan` was hit. No stack overflows, no LVGL corruption, on
top of an already-larger `settings_server` (8 registered routes now).

Known rough edges, not yet addressed:
- The HA token is stored in NVS in **plaintext** - no flash/NVS encryption.
  Fine for bring-up, worth hardening before anything ships (matches the
  same "flag it, don't build it now" treatment as the open Wi-Fi setup AP).
- `/scan`'s mDNS query is synchronous inside the HTTP request (blocks
  ~3s) - acceptable for a manually-triggered button, would need to become
  asynchronous if it were ever auto-triggered on page load.

## Speaker was silent - I2S mono slot mode (2026-08-11)

User feedback after this round: display, Wi-Fi, mDNS, and the settings
server all confirmed working live - but no sound from the startup tone.

`hardware/audio` configured the I2S bus with
`I2S_SLOT_MODE_MONO`. Re-checked Waveshare's own audio reference for this
exact board (`Example/ESP-IDF/05_Audio_Test`) and it always opens the
ES8311 with `channel = 2` (stereo), even though the board only has one
speaker. Most inexpensive I2S DAC/ADC codecs, ES8311 included, only
implement the full stereo Philips frame; ESP-IDF's I2S "mono slot" mode
sends just one slot per LRCK cycle, which this class of codec doesn't
handle and reads as silence rather than falling back to something audible.

Fixed by switching to `I2S_SLOT_MODE_STEREO` and duplicating each mono
sample onto both L/R slots when writing
(`audio_play_startup_tone()`)/reading (`audio_measure_mic_level()`,
peak taken across both slots regardless of which one is real).

**Confirmed on hardware**: audible, but a very quiet, very short "plouc" -
the stereo fix was the right call, the sound itself (single 880Hz tone,
180ms, amplitude 6000/32767) was just underwhelming as a product sound.

## Startup chime redesign, attempt 1 - simultaneous chord (2026-08-11)

Replaced the single debug beep with a four-voice "sunrise" chord swell (F
major - root/third/fifth + an octave-up shimmer voice, all four sounding
*at once*), staggered entrances, ~1.2s total, ES8311 volume raised 70->92.

**User verdict: "sounds like an ocean liner foghorn."** Root cause:
playing several tones simultaneously on a small single-driver speaker
produces audible beat frequencies between the close-together tones (349Hz
vs 440Hz beats at 91Hz, 440 vs 523 at 83Hz, etc.) - right in the "foghorn
wah-wah" perceptual range, and a small speaker's nonlinearity has no
headroom to keep a 4-note stack clean regardless.

## Startup chime redesign, attempt 2 - sequential arpeggio (2026-08-11)

Replaced the chord with a **rising arpeggio**: the same four F-major notes
(F4, A4, C5, F5), but played **one at a time** (130/130/140/420ms, the
last one held with a graceful release; ~820ms total) - never more than one
tone sounding, so there is nothing left to beat against. Each note has its
own short raised-cosine attack (12ms) and release that reaches exactly 0
at the note's own end, so transitions between notes are click-free without
needing to matching phase across different frequencies. Volume backed off
from 92 to 85 (up from the original 70, short of the 92 that may have been
part of what pushed the chord into distortion too). Single voice at a
time, so headroom is less of a concern than it was for the stacked chord.

Builds clean, boots without crashing (the ~820ms delay before Wi-Fi
connect in the boot log is this chime playing synchronously, shorter than
attempt 1's ~1.2s, as expected).

**User verdict: still "absolutely awful."** Rather than attempt a third
redesign blind, the automatic startup chime is **disabled** for now
(`main.c` no longer calls `audio_play_startup_tone()` on boot) so it stops
interrupting testing/waking the household. `audio_play_startup_tone()`
itself is untouched and still reachable manually from `/debug/speaker` on
the settings server, for whenever sound design work on it resumes
deliberately instead of iterating blind between builds.

## Web UI redesign: topbar + sidebar, MDI icons, AP toggle (2026-08-12)

Replaced the single flat settings page with an actual small app shell,
built directly into `networking/settings_server` (no separate frontend
build, no JS - everything is server-rendered HTML with inline SVG icons,
consistent with every other page on this device):

- **Topbar**: the Helios sun mark (inline SVG, path extracted directly
  from `assets/brand/helios-logo.svg`) + "Helios Mini" on the left; live
  Wi-Fi and Home Assistant connection status icons on the right (green/red/
  grey depending on `diagnostics_get_status()` and the stored
  `ha_client_status_t`).
- **Sidebar**: Network / Home Assistant / Debug, icon + label, active item
  highlighted. Collapses to icons-only below 640px width (pure CSS media
  query, no JS) so it stays usable on a phone.
- **Icons are real MDI** (Material Design Icons - `wifi`, `home-assistant`,
  `lan`, `bug`, `magnify`, `monitor`, `volume-high`, `microphone`,
  `access-point`/`access-point-off`, `refresh`, `content-save`), the same
  icon set Home Assistant's own frontend uses. Path data extracted directly
  from the `@mdi/js` package in the user's local `ha-frontend` checkout
  (`node_modules/@mdi/js/mdi.js`) rather than reconstructed from memory,
  and embedded as `firmware/networking/settings_server/mdi_icons.h`.
  Icons are sent as separate `httpd_resp_sendstr_chunk()` calls around the
  raw path string (never through a bounded `snprintf`), which also
  sidesteps the format-truncation bug class from earlier in this file.

Routes reorganized to match the new information architecture:

| Old | New |
|---|---|
| `/` (WiFi+HA mixed) | `/network` (Wi-Fi status + setup-AP toggle) |
| `/save`, `/test`, `/scan` | `/ha`, `/ha/save`, `/ha/test`, `/ha/scan` |
| `/debug` and sub-paths | unchanged, just re-skinned; gyroscope sentence removed entirely per user request rather than kept as a caveat |

**New: setup-AP toggle on `/network`.** `wifi_sta_set_setup_ap_enabled()`
switches `WIFI_MODE_STA` <-> `WIFI_MODE_APSTA` at runtime and (re)configures
the `HELIOS-MINI-XXXX` AP, without tearing down the station connection or
the already-running `settings_server` - because `esp_http_server` binds to
all interfaces by default, the *same* server instance becomes reachable at
`http://192.168.4.1/` the moment the AP comes up, no second server needed.
This is separate from `networking/provisioning`'s own AP (which only ever
runs before any Wi-Fi credentials exist) - this one lets a second device
join and reach the settings app on demand, any time after the device is
already on the home network.

**Verified live**: fetched `/network`, `/ha`, `/debug` over the LAN with
`curl` (200s, well-formed - balanced `<div>`/`<svg>` tag counts checked,
no gyroscope mentions), then exercised the AP toggle end-to-end through
the running device (`/network/enable-ap` -> page confirms "Active", station
connection stayed reachable throughout -> `/network/disable-ap` -> reverts).
No crashes, no new warnings in the serial log. Visual review by the user
(not just curl/tag-balance checks) is still pending.

## Energy rings + `/display` settings (2026-08-13)

The on-screen UI: three concentric Apple-Watch-style rings (solar / grid /
battery, outer to inner) plus the home's current consumption as big text in
the middle, backed by a new `/display` page on the settings server where
every Home Assistant entity and ring limit gets configured, with a live
per-entity status panel. New components, dependency direction kept strict
to avoid cycles:

```
ui/home (energy_rings.h/.c)        <- pure LVGL renderer, REQUIRES display only
  ^
helios/energy_model                <- polling/business logic, REQUIRES storage ha_client home
  ^
networking/settings_server         <- /display page, REQUIRES energy_model (+ everything it already had)
```

`helios/energy_model` deliberately does **not** depend on
`networking/settings_server` even though it needs the stored Home Assistant
URL/token - it re-reads those two NVS keys (`ha_url`/`ha_token`) directly
with a tiny local `load_ha_credentials()`, rather than pull in the whole
settings app as a dependency just for two `storage_get_string()` calls.

**Entity model** mirrors the parent Helios HA card's `chip-appearance.ts`
exactly: 6 independently-optional entities (solar, grid import, grid
export, battery charge, battery discharge, home), same fallback colors
(`#ff9800` / `#488fc2` / `#8353d1` / `#f06292` / `#4db6ac`). Battery is two
separate power entities, not one signed value - same reasoning as Helios.
`ENERGY_DEFAULT_MAX_SOLAR_W = 5000` reuses Helios's own
`DEFAULT_MAX_EXPECTED_POWER_W`.

**`helios/ha_client`** gained `ha_client_get_entity_power()`: GET
`<url>/api/states/<entity_id>`, parsed with cJSON (ESP-IDF's built-in
`json` component - `REQUIRES` needed adding it alongside `esp_http_client`).
Returns one of six outcomes (`ha_entity_status_t`): OK, not configured, not
found (404), unavailable/unknown state, not-numeric state, unauthorized,
unreachable - this is what makes the status panel able to "gere TOUS LES
CAS" instead of just showing a blank ring. `kW`/`MW`
`unit_of_measurement` attributes are detected and normalized to watts
automatically; anything else (plain `W` or no unit) is taken as already
being watts.

**`helios/energy_model`** polls all 6 entities every 10s in a background
task, decides which direction is "live" for the two two-entity rings
(grid import vs. export, battery charge vs. discharge - whichever is
above a 5W noise threshold, defaulting to import/discharge on a tie),
computes each ring's fill % against its configured max, and calls
`energy_rings_update()`. The very first time the `home` entity gets
configured, it swaps the screen from the persistent IP notice
(`ui/animations/network_status`) over to the rings permanently -
there's no way back to the IP screen without rebooting, matching how the
rings are meant to become the device's actual home screen once set up.
`energy_model_refresh_config()` (a binary semaphore the poll task blocks
on with a timeout) lets `/display/save` force an immediate re-poll instead
of waiting up to 10s.

**`/display`** (GET) shows the live entity status panel (reading the
poller's last snapshot via `energy_model_get_status()`) followed by a
single combined form: 6 entity-id text inputs + 4 ring-limit number
inputs, one Save button. **`/display/save`** (POST) persists both, wakes
the poller, then - like `/ha/save` before it - synchronously re-tests
every entity that was actually filled in and renders the same status
panel with fresh results, so saving gives immediate feedback instead of
"wait up to 10s and reload". Every status panel row is exactly one of
five states, never a blank: not configured / Home Assistant not set up /
not tested yet / a real `ha_entity_status_t` error / OK with the live
value.

### Bug: empty dynamic strings silently truncated every page

Found while first testing `/display` live on a fresh device (all 6 entity
fields empty, as intended before anyone configures anything).
`GET /display` came back HTTP 200 with a plausible `Content-Length`-free
chunked body that just... stopped, mid-attribute, no closing tags:

```
...<input type='text' name='ent_solar' placeholder='sensor.solar_power' value='
```
*(response ends here - no closing quote, no `</form>`, no `</html>`)*

Root cause: every dynamic string on every settings-server page goes
through `httpd_resp_sendstr_chunk(req, s)`, which is a thin wrapper over
`httpd_resp_send_chunk(req, s, strlen(s))`. Per esp_http_server's own
contract, calling `httpd_resp_send_chunk()` with a **zero-length buffer**
is the documented way to end a chunked response - the exact same call
shape used deliberately once at the very end of `close_page()`
(`httpd_resp_sendstr_chunk(req, NULL)`). An *empty* dynamic value -
an unconfigured entity id, here - produces `strlen(s) == 0` and hits that
same "end the response now" path by accident, mid-page.

This was latent in `/ha` too (`escaped_url` is empty before Home Assistant
is ever configured) but had never actually been exercised empty in a live
test, since Home Assistant was configured on the test device before `/ha`
was first curled. `/display` has *six* such fields and is the first page
normally viewed with all of them empty (a fresh device), which is why it
surfaced here.

**Fix**: added `send_chunk(req, s)` in `settings_server.c` - skips the
call entirely for `NULL`/empty strings (a no-op for the HTML either way)
and otherwise forwards to `httpd_resp_sendstr_chunk()`. Every dynamic
`httpd_resp_sendstr_chunk()` call site in the file (~150) now goes through
it; the one deliberate `NULL` terminator in `close_page()` was kept as a
direct call, on purpose, since that *is* the real end-of-response signal
and must not be skipped.

**Verified live** after the fix: `/display`, `/ha`, `/network`, `/debug`
all fetched over the LAN with `curl` and each response now ends in
`</html>` (checked by hand, not just tag-balance counting this time).
Then exercised `/display/save` twice against the real board: once with a
deliberately wrong entity id (`sensor.does_not_exist_xyz`) for `home` -
the status panel correctly came back `no such entity in Home Assistant`
(a real 404 round-tripped from the user's actual Home Assistant instance,
confirming auth, the REST call, and the JSON/status-code handling all
work end-to-end) - then again with everything cleared back to empty to
leave the device in a clean, unconfigured state. No crashes or reboots
in the serial log across any of this.

## Auto-discovery via Home Assistant's WebSocket + Energy Dashboard, power-only (2026-08-13)

Superseded the manual entity-entry design from the previous session before
it ever shipped to the user: **"Je ne veux pas devoir renseigné mes
entités manuellement, je veux que l'on recupere directement les valeurs
en live depuis le websocket d'HA."** Replaced `/display`'s 6 free-text
entity fields with zero configuration: Helios Mini reads Home Assistant's
own Energy Dashboard configuration (Settings -> Dashboards -> Energy)
directly, live, over a websocket, using **only** each source's linked
live power (W) entity - never its cumulative energy (kWh) statistic, not
even as a fallback. Getting to that final rule took three iterations, all
corrected live against the user's own Home Assistant instance rather than
guessed - see "Three real corrections" below; this section describes the
design that's actually running.

**New component `helios/ha_ws`** - a Home Assistant WebSocket client
(`espressif/esp_websocket_client` managed component, resolved to 1.8.0):

1. Connect to `<url>/api/websocket` (http(s) -> ws(s) scheme swap +
   `/api/websocket` appended). `esp_websocket_client`'s built-in
   auto-reconnect (`reconnect_timeout_ms`) handles Wi-Fi drops and HA
   restarts without any manual retry loop.
2. Handshake: HA sends `auth_required` first (not the client) -> reply
   `{"type":"auth","access_token":...}` -> HA replies `auth_ok` or
   `auth_invalid`.
3. On `auth_ok`, send `{"type":"energy/get_prefs"}`. Each
   `energy_sources[]` entry can carry a live power companion entity - the
   same one the parent Helios HA card's own power chips read: `stat_rate`
   on a solar source, `power_config.stat_rate_from`/`stat_rate_to` on a
   grid or battery source (`stat_rate_from` = import/discharge,
   `stat_rate_to` = export/charge). **Only these are ever used.** The kWh
   statistic id(s) that always sit alongside them in the same response
   (`stat_energy_from`/`stat_energy_to`, for HA's own billing/history
   graphs) are read out of the JSON only far enough to log them for
   debugging - never subscribed to, never a fallback. A ring with no
   power entity linked in HA's Energy Dashboard is simply reported not
   configured.
4. A ring can be fed by more than one power entity (uncommon, but the
   schema allows it, e.g. multiple solar strings or battery packs) -
   every one found is subscribed and summed, not just the first.
5. Per discovered entity: `{"type":"subscribe_trigger","trigger":{"platform":"state","entity_id":...}}`
   - a single-entity automation-style state trigger, not the firehose
   `subscribe_events`/`state_changed` (which would hand this ESP32 every
   state change on a ~1250-entity instance) and not the compact
   `subscribe_entities` diff format either (its add/change delta-merging
   isn't worth the embedded complexity here). One `subscribe_trigger` per
   entity means HA itself filters server-side - the board only ever
   receives events for exactly what it asked for.
6. Immediately after subscribing, one REST call
   (`ha_client_get_entity_power()`) bootstraps a starting reading - the
   ring goes live before HA even sends its first push update, no waiting
   for a second sample the way a derivative-based approach would need.
7. Every subsequent `to_state` for a subscription is used directly
   (kW/MW unit-converted to W, same conversion the REST bootstrap
   applies). A source with no new sample in `HA_WS_STALE_AFTER_S` (10
   minutes) is reported `HA_WS_SOURCE_STALE`; when a ring sums several
   contributors, staleness uses the *freshest* one, not the oldest.
8. Websocket frames arrive fragmented across multiple
   `WEBSOCKET_EVENT_DATA` callbacks (`payload_offset`/`payload_len`); a
   small reassembly buffer accumulates each frame before handing the
   complete JSON string to a queue - the event callback itself (which
   runs on the client's own internal task) never blocks on cJSON parsing
   or the REST bootstrap call, both of which happen on `ha_ws_task`
   instead.

`helios/energy_model` no longer polls anything - it starts `ha_ws` and
every 3s turns its live status + the still-manually-set ring limits
(`energy_config.h`, unchanged) into ring percentages/colors. Home
consumption (the center text) has no Energy Dashboard entity to discover
either - HA's own dashboard draws its consumption graph as
`solar + grid_import - grid_export`, so that's what Helios Mini computes
too, straight from the already-live per-source power values.

`/display` is a read-only auto-discovery status panel (which power
entity/ies Home Assistant reported for each ring, comma-joined if more
than one, live value or why not) plus a "Rescan Energy Dashboard" button
(`ha_ws_rescan()`, for after the user edits their Energy Dashboard config
in HA) and the ring-limits form (unchanged, since Home Assistant has no
concept of Helios Mini's display scale).

### Three real corrections, all against the live instance, not guesses

Every one of these was only caught because the whole loop - flash, log,
curl, read the user's actual `energy/get_prefs` response, fix, reflash -
happened against the user's real Home Assistant instance (AIUR,
`192.168.0.2:8123`), not assumed from the API docs or judged from the
terminal alone:

1. **Wrong grid schema, silently found nothing.** The API docs describe
   grid sources as `flow_from: [{stat_energy_from}]` / `flow_to: [{stat_energy_to}]`
   arrays; the user's real instance returns `stat_energy_from`/
   `stat_energy_to` flat on the source object instead, and has *three*
   separate `"grid"` entries (two Linky tariff-period meters, one Shelly)
   rather than one. **User:** "Grid import et export sont configurés dans
   mon dashboard energie (et Helios fonctionne avec) alors que là, ca me
   ressort qu'ils ne sont pas configurés." Fixed by logging the raw
   `energy/get_prefs` response in full and reading exactly what the
   user's HA actually sends.
2. **Deriving power from energy when a live power entity was right
   there.** The first fix (still summing kWh deltas) missed that the same
   response already carried `stat_rate`/`power_config.stat_rate_from`/
   `stat_rate_to` - real power entities. **User:** "pourquoi tu ne prends
   pas directement les valeurs des entities puissance du dashbaord ! [...]
   Je t'ai dit de te baser sur la carte Helios et sur le fonctionnement
   des chips d'Helios !" First attempt at this fix introduced a subtler
   bug: `power_config.stat_rate_from` (grid import power) sitting on the
   Shelly's *export*-typed grid entry got attached to grid import too,
   double-counting the same physical import against the Linky meters
   already tracking it there. Corrected mid-fix by only trusting a
   direction's power companion when that same object's matching
   `stat_energy_from`/`stat_energy_to` was also set.
3. **Any energy fallback at all was the wrong idea.** Even after fixing
   #2, a ring with no power companion (the Linky meters) still fell back
   to deriving power from their kWh deltas - which is exactly what
   surfaced the direction-gating bug above, and produced a real but
   wrong-feeling result (`sensor.lixee_easf02, sensor.lixee_easf01` on
   the grid import row). **User:** "C'est pas DU TOUT DES ENTITIES DE
   PUISSANCE ça... et encore moins celles qui sont configurées !",
   followed by: **"ON NE RECUPERERA JAMAIS des entités d'ENERGIE. on ne
   se base QUE sur la puissance."** Removed the energy-derivative path
   entirely - `ha_client_get_entity_raw_value()` (added for it) was
   deleted outright, not just left unused. Discovery now does one pass
   per ring collecting every power-entity candidate found anywhere in
   `energy_sources[]`; a ring with zero power candidates is reported not
   configured, full stop, regardless of what kWh statistics exist for it.

### Stack overflow from summing multiple sources

Summing multiple contributors per ring meant `apply_energy_prefs()` grew
locals proportional to `SLOT_COUNT * HA_WS_MAX_SUB` - the exact same class
of bug as the `wifi_ap_record_t aps[20]` stack overflow from
`networking/provisioning` earlier in this project (see above), and it hit
for real on this hardware the moment multi-source grid data actually
flowed through it (crash-loop reported live: "ca demarre en boucle").
Fixed the same way as last time: heap-allocate instead of a stack local.
A second stack overflow followed immediately after in the `energy_model`
task - `ha_ws_status_t` grew to ~2KB once its entity-id fields widened to
fit several comma-joined ids, declared as a local in `energy_model.c`'s
`render()`, on a task stack sized for the old, smaller struct. Fixed by
sizing both tasks' stacks with real headroom instead of the minimum that
happened to work before.

**Verified live**, on the final power-only design: solar, grid import,
and grid export all went live *immediately* on connect (one REST
bootstrap each, no waiting for a push update), showing real wattages
(`6 W`, `907 W`, `0 W`) - all three from genuine power entities
(`..._puissance`, `..._puissance_consommation`, `..._puissance_restitution`),
zero Linky/kWh entities anywhere in the result. Battery correctly shows
not configured (none set up in the user's Energy Dashboard).
`/display/rescan`, `/ha`, `/network`, `/debug` all still fetch clean over
`curl`. No crashes after the stack fixes; free heap settled around 58KB
after boot + several page loads (down from ~89KB pre-websocket, from the
larger task stacks now required - not concerning on an 8MB-PSRAM board,
no sign of a leak, but not stress-tested over a long run either).

### Home consumption formula corrected to match Helios exactly

The center-text formula (`solar + grid_import - grid_export`, clamped at
0) was an independent guess at "how HA's Energy Dashboard draws its own
consumption graph" - reasonable-sounding, but not actually verified
against the parent Helios card, and missing the battery term entirely.
**User:** "la logique de calcul de la consommation de la maison est à
reprendre sur Helios, actuellement, tu ne fais qu'afficher la connexion à
l'import grid en consommation." Checked the real source this time
(`~/Developer/helios/src/core/energy.ts`, `consumptionLoad()`) instead of
guessing again:

```ts
// load = production + gridImport - gridExport - netBattery
// (netBattery = charge - discharge, charge positive), clamped at 0.
```

`helios/energy_model`'s `render()` now mirrors this exactly, including
the `netBattery = charge - discharge` term it was missing (with no
battery configured on the test instance, `charge_w`/`discharge_w` are
both 0, so this specific fix wasn't independently visible live - but the
formula now matches Helios's own identity verbatim rather than an
approximation of it).

### Ring visual polish: rounded caps, knob dot removed

**User:** "profites en pour enlever le point jaune qui apparait au centre
du logo d'Helios et fais en sorte que les anneaux soient 'fermée' avec un
arrondi, comme les anneaux de l'apple watch." Two fixes in `ui/home/energy_rings.c`:
`lv_arc`'s default theme draws a small draggable "knob" circle at the
indicator's end (meant for interactive sliders) - `lv_obj_remove_style()`
alone doesn't reliably strip every theme-applied style, so every visual
property that could paint it (`bg_opa`, `border_width`, `shadow_width`,
`outline_width`, padding) is now overridden explicitly to fully suppress
it. Both arc parts also switched from `arc_rounded(false)` to `true` for
rounded caps - the actual Apple Watch activity-ring look, which the
original implementation never had despite the ring design being
explicitly modeled on it.

## Further ring/UI polish, same live session (2026-08-13)

A rapid round of visual feedback direct from the physical board, each one
built, flashed, and confirmed in turn:

- **Boot logo had its own separate stray dot, not the ring knob.**
  **User:** "j'ai toujours le point jaune au milieu du logo helios au
  demarrage aussi (sur l'ecran)." The boot animation's small "central
  light" dot and its circular sweep arc (`ui/animations/boot_animation.c`)
  were created, animated, and then simply never deleted or faded out once
  the Helios logo faded in over them - both sat there, amber dot dead
  center, for as long as the boot scene stayed on screen. Fixed by
  deleting both the moment the logo takes over (`arc_sweep_done_cb()`).
- **Rings animate instead of jumping.** **User:** "il faudra aussi faire
  une animation de l'anneau quand il change de valeur, un vrai lerp, pas
  un decallage brut." `ui/home/energy_rings.c` now runs every ring value
  change through an `lv_anim_t` (700ms, ease-out), canceling any
  transition already in flight for that ring first so rapid updates don't
  race each other.
- **Ring track removed; a real gap where a ring is at 0% or not
  configured.** **User:** "Tu peux aussi enlever les fonds des anneaux.
  Pour les anneaux qui sont à zero, on dessine quand meme la fin et le
  debut de l'anneau (un disque dans ces cas là vu que la fin et de lébut
  sont à la meme valeur)." The always-visible dark gray background track
  is gone - only the colored indicator draws now. First attempt assumed a
  literal 0-length rounded arc would still render as a dot (start and end
  caps coinciding); it doesn't - LVGL skips drawing an arc with nothing to
  sweep, rounded caps or not, so the fix left rings at 0%/unconfigured
  with no mark at all. **User:** "il manque le debut et la fin des
  anneaux à 0 ou non configurés." Corrected by flooring the indicator's
  value to a hair over 1 degree of sweep (`target < 3` out of the
  0-1000 range) rather than a true 0, so the rounded cap always has just
  enough arc to actually render as a small dot.
- **Center text: icon instead of a text label.** **User:** "Pas de texte
  non plus 'Home consumption', on met l'icone maison MDI au dessus de la
  valeur numerique affichée." The "home consumption" caption under the
  number is gone; a small MDI house icon (`mdiHome`, rasterized via the
  existing `tools/asset-gen/svg_to_lvgl.py` pipeline into
  `ui/home/assets/mdi_home_icon.c`, same tool used for the boot logo) now
  sits above it instead. The status/error caption (e.g. "no source in
  your Energy Dashboard") still uses that same label slot when there's
  actually something to say - it's just empty text, not text at all, on
  the happy path.
- **Home consumption now matches Helios's own formula exactly.**
  **User:** "la logique de calcul de la consommation de la maison est à
  reprendre sur Helios, actuellement, tu ne fais qu'afficher la connexion
  à l'import grid en consommation." The previous formula (`solar +
  grid_import - grid_export`) was an independent, unverified guess -
  missing the battery term entirely. Checked the real source this time
  (`~/Developer/helios/src/core/energy.ts`, `consumptionLoad()`) instead
  of guessing again: `load = production + gridImport - gridExport -
  netBattery` (`netBattery = charge - discharge`, charge positive),
  clamped at 0. `helios/energy_model`'s `render()` now mirrors it
  verbatim.
- **Number format preference: W/kW toggle + 0-3 decimal places.**
  **User:** "je veux un toggle dans 'Display' qui permet de basculer
  l'affichage en W ou en kW (W par défaut) et de définir le nombre de
  decimal (un slider entre 0 et 3), 1 par défaut." Added
  `energy_format_t` (`energy_config.h`, NVS-backed, same pattern as ring
  limits) and a single shared `energy_format_power()` helper used by
  *both* the rings' center text and `/display`'s status-panel values, so
  there's exactly one formatting implementation instead of two that could
  drift apart. `/display`'s existing limits form gained a unit `<select>`
  and a decimals `<input type='range'>`, one Save button covering both
  limits and format together.
- **Settings-page prose: no em dashes, more paragraph breaks.**
  **User:** "aéré et par pitié, pas de cadratins." The `/display` hint
  text and connection-status strings used the project's usual
  `word - word` dash-as-aside style throughout; rewritten as separate
  sentences/short paragraphs with plain punctuation (commas, parentheses)
  instead.
- **Status-panel pill wrapped to two lines on long rows.** A `.pill`
  badge (e.g. solar's "5 W") wrapped its own text vertically whenever the
  row's label + long entity id pushed the row tight, because flexbox
  shrinks items with no `flex-shrink:0` by default. Added
  `flex-shrink:0;white-space:nowrap` to `.pill` and `min-width:0;
  overflow-wrap:anywhere` to `.row .label` so the label wraps/shrinks
  instead of the badge.

All of the above verified live: rebuilt, reflashed, and reconnected to
the user's real Home Assistant instance after every change in this
round, no crashes, `/display`'s new unit/decimals fields round-tripped
correctly through `/display/save` (confirmed switching to kW + 2
decimals persisted and reflected back in the status panel).

## Boot animation redesign + white flash fix, "V1" round (2026-08-13)

**User:** "ok pour la v1, il ne nous manque plus grand chose : L'animation
de chargement, on oublie l'anneau qui se remplit avec le point au milieu.
A la place le logo d'helios ou chacune des 12 flammes du soleil se
dessine au fur et a mesure du chargement. On n'affiche plus l'adresse IP
au demarrage si on est connecté à Home Assistant."

**Boot logo draws itself in, flame by flame.** `assets/brand/helios-logo.svg`
turns out to already be one `<path>` with 13 separate "M...Z" subpaths -
the 12 sun-flame rays plus the central disc - discovered by literally
counting them rather than assuming. That's what makes a piece-by-piece
reveal possible without hand-splitting the artwork. New tool
`tools/asset-gen/svg_pieces_to_lvgl.py`: rasterizes each subpath alone at
the same px-per-SVG-unit density as a full 440px render, then crops it to
its own alpha bounding box (a single flame is a small fraction of the
canvas) before baking it into an LVGL image - the 13 pieces together
(~365KB raw) end up *smaller* in flash than the one old flattened 440x440
image (~756KB) did. `boot_animation.c` was rewritten from scratch:
`ui/animations/assets/helios_logo.c` (and the dot+arc+single-fade scene
it drove) is gone, replaced by 13 small `lv_image` objects positioned via
offsets baked into `helios_logo_pieces[]` at generation time, each fading
in on its own staggered `lv_anim_t`.

Getting the reveal order and feel right took several rounds of direct
feedback, each rebuilt and reflashed in turn:

- First attempt revealed the rays in the SVG's own source order, which
  turned out to sweep counter-clockwise starting near 10 o'clock.
  **User:** "dommage, dans l'autre sens l'affichage des petales du soleil
  (sens horaire)" - reversed into a `s_reveal_order[]` lookup table so
  the pieces stay in their original array (nothing else has to change)
  but get revealed in a different sequence.
- The reversed order still didn't start at the top. **User:** "ca aurait
  ete bien de commencer par celui tout en haut :s" - computed each ray's
  clock-angle from its `(center_dx, center_dy)` offset
  (`atan2(dx, -dy)`) to find the one actually closest to 12 o'clock
  (index 10, ~2 degrees) and made that the start of the sequence, sweeping
  clockwise from there, disc always last.
- The fade itself was too quick to see. **User:** "j'aimerai que tu les
  fasses s'afficher en fading" then "et le fading, plus long aussi, on a
  a peine le temps de le voir." Per-piece fade went 130ms -> 220ms ->
  400ms, stagger 70ms -> 90ms, with an explicit `lv_anim_path_ease_out`
  (total sequence now ~1.5s, still comfortably under the <3s boot
  target).

**White flash at power-on, found and actually fixed.** **User:** "le
flash blanc au tout debut, on est obligé de l'avoir ?" First attempt:
added `s_first_flush_done` (set from the panel IO's `on_color_trans_done`
ISR) and had `lvgl_task` only ramp brightness to full once the first real
LVGL frame had actually reached the panel, instead of turning brightness
up inside `panel_init()` before LVGL even existed. **User:** "il est
toujours là" - the fix was real but incomplete: `s_lcd_init_cmds[]` (the
CO5300 init table, ported from Waveshare's factory firmware) already sent
`{0x51, 0xFF}` - full brightness - as its *sixth* command, well before
Sleep Out (`0x11`) and Display On (`0x29`) even ran, let alone before my
later brightness-zero call. The panel was unblanked at full brightness
against whatever garbage was in GRAM for the entire rest of init
regardless of anything done afterward. Fixed at the actual source: that
table entry is now `{0x51, 0x00}` - brightness starts at 0 before Sleep
Out/Display On ever run, and only the `s_first_flush_done` mechanism from
the first attempt ramps it up, once real content is confirmably on
screen.

**No IP notice when Home Assistant is already configured.** `main.c` now
checks `settings_get_ha_config()` before deciding whether to register
`wifi_sta_set_connected_cb(network_status_show_connected)` at all - if
Home Assistant credentials are already saved, the device is heading for
the energy rings anyway once it connects, so the IP notice (previously
always shown the moment Wi-Fi came up) is skipped entirely and the boot
logo stays up until the rings take over. Still shown as before when Home
Assistant isn't configured yet, since that's the only way to find the
settings app's address in that case. Tradeoff worth flagging: if Home
Assistant is configured but the connection or Energy Dashboard discovery
never succeeds, the screen has no fallback showing the IP either - not
addressed this round.

All rebuilt and reflashed after each change; no crashes throughout.

**Boot logo halved in size.** **User:** "tu vas reduire de 50% la taille
du logo Helios, je veux que ca fasse tres 'apple' au demarrage, sauf qu'a
la place de la pomme, c'est le logo Helios, on garde l'animation des
flammes du soleil." Since `svg_pieces_to_lvgl.py` rasterizes each piece
at `canvas_px`-per-512-SVG-units and bakes each piece's on-screen offset
from that same density, regenerating at `canvas_px=220` instead of `440`
scales every piece's size *and* position together automatically - no
changes needed in `boot_animation.c` itself. Also shrank the flash
footprint further (13 pieces at half linear size are a quarter the raw
pixels). Board briefly stopped responding to `idf.py flash` entirely (zero
bytes either direction on serial, not just an esptool handshake retry) -
this board's power rail depends on the firmware re-asserting the GPIO18
latch quickly after each PWR-button wake (see the very first entry in
this doc), so anything that drops power without a fresh button press
leaves it fully unpowered from USB's perspective. Fixed by the user
pressing PWR again; flashed and verified clean immediately after.

## Still open (need eyes/ears on the physical board)

- [x] Panel orientation (`MADCTL = 0xC0`) and the doubled boot logo -
      confirmed good by the user (2026-08-11).
- [x] Startup chime is audible (speaker path itself works, confirmed via
      two rounds of user listening) - **disabled on boot anyway**, pending
      a sound design that isn't "absolutely awful"; still reachable at
      `/debug/speaker` for whenever that resumes.
- [ ] Confirm the power-on white flash is actually gone now - the real
      cause (full brightness baked into `s_lcd_init_cmds[]` itself, well
      before the panel is even unblanked) was fixed this session, but
      hasn't been re-confirmed live after the fix. The first attempt at
      this looked right in code but the user still saw it, so treat this
      one as unconfirmed until watched again.
- [ ] No on-screen fallback if Home Assistant is configured but never
      actually connects or its Energy Dashboard never gets populated -
      the IP notice is skipped whenever HA credentials are saved (see
      "V1 round" above), with no fallback screen if that trust turns out
      to be misplaced. Not hit in practice yet, flagged as a real gap.
- [ ] Confirm the microphone level test (`/debug/mic`) actually responds to
      real sound - the stereo-mode fix applies to the RX path too but
      hasn't been tried at all yet, silent or not.
- [ ] Visual review of the new `/network`, `/ha`, `/debug` pages (topbar,
      sidebar, icons, AP toggle) - only checked so far via `curl` and tag
      counting, not by a human actually looking at it in a browser.
- [ ] Confirm `TE` (GPIO 9) is safe to leave unconnected in software for V0.1
      (tearing may be visible without it; acceptable for bring-up, revisit
      for V0.3 UI polish).
- [ ] Confirm touch coordinate mirroring (`hardware/display`'s
      `touch_read_cb`) is correct now that `MADCTL` is set - ported from the
      vendor example (which also uses `0xC0`) so it should already match,
      but hasn't been touched yet to confirm; verify by touching the four
      edges.
- [ ] Confirm the power-latch behavior (GPIO 18) end-to-end in the
      USB-only configuration: does the board actually stay powered after
      the PWR button is released once V0.1 asserts the latch, exactly as
      it does in the vendor's battery-equipped reference?
- [x] Visual review of the energy rings on the actual round panel - the
      user watched it live this session and drove several real fixes
      from what they saw (boot logo's stray dot, missing knob-vs-track
      dot at 0%/unconfigured, hard-jump ring updates, "home consumption"
      text). Not yet re-confirmed after the *latest* round (animated
      transitions, the always-visible start/end dot, the house icon) -
      those were fixed and flashed but not yet looked at again.
- [ ] Confirm the grid ring actually auto-switches from blue (import) to
      purple (export) with real data over time - the user's Shelly
      reports both directions live, just hasn't been watched switch yet.
- [ ] Battery ring untested end-to-end - nothing configured in the user's
      Home Assistant Energy Dashboard for it yet. Should auto-discover
      correctly once they add a battery source with a linked power
      entity (`ha_ws_rescan()` via the "Rescan Energy Dashboard" button
      on `/display`, no firmware change needed), but not actually tried.
- [ ] Longer-run stability of `helios/ha_ws` - reconnect behavior across a
      real Wi-Fi drop or Home Assistant restart, and whether free heap
      stays flat over hours (only spot-checked a few times so far, no
      leak evidence but no long run either).
