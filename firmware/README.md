# Helios Mini Firmware

ESP-IDF firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.32, targeting the
**V0.1 — Hardware Bring-Up** milestone from `docs/SPEC.md` (Section 34):
display, touch, Wi-Fi, speaker, microphone, buttons, USB — plus, ahead of
schedule, the Wi-Fi provisioning slice of **V0.2** (spec Section 16): a
SoftAP + browser setup page, no more hardcoded dev credentials required.

**Home Assistant pairing deliberately does not follow spec Sections 17-18**
(discovery + a dedicated custom integration). Instead, once connected to the
home network, the device runs its own small settings server (reachable at
the IP permanently shown on screen) that can find Home Assistant on the LAN
via mDNS, takes a URL + a Long-Lived Access Token, and tests the connection
immediately — simpler to build and to use, at the cost of one manual step
(generating the token in HA) instead of a one-tap pairing button. The same
server also has a `/debug` page with hardware self-tests (screen, speaker,
microphone). See "Home Assistant settings" below. The Helios UI, OTA, and
full diagnostics come in later versions.

## Status: running on hardware, Wi-Fi setup verified end-to-end

Builds with **zero errors and zero warnings** against ESP-IDF 5.5.1
(`idf.py set-target esp32s3 && idf.py build`, verified with the actual
resolved managed-component versions: `lvgl/lvgl 9.5.0`,
`espressif/esp_lcd_sh8601 2.0.1~1`, `espressif/button 4.2.0`,
`espressif/es8311 1.0.0~1`).

Two crashes were hit and fixed on the very first hardware runs (both are
now confirmed fixed on real hardware — full root cause and fixes in
`docs/HARDWARE_REFERENCE.md`):

1. A task-watchdog crash loop: `boot_animation_start()` touched LVGL from
   the `main` task without the display component's lock, racing the
   `lvgl` task's own `lv_timer_handler()` loop.
2. A stack overflow in the `httpd` task the moment a phone actually
   connected to the setup portal: the Wi-Fi scan-results buffer was too
   large for the default 4KB task stack.

After both fixes, a full live run succeeded: a phone joined the
`HELIOS-MINI-XXXX` AP, loaded the setup page, submitted real Wi-Fi
credentials, and the device saved them, rebooted, connected in station
mode, and got a DHCP lease — the complete Wi-Fi setup flow, working
end-to-end on hardware.

### Wi-Fi setup flow (implemented)

On first boot (no stored credentials), Helios Mini starts an open Wi-Fi
network named **`HELIOS-MINI-XXXX`** (`XXXX` from the station MAC) and a
plain HTTP server at **`http://192.168.4.1/`**. The screen shows the AP name
and URL. Connect a phone/laptop to that network, open the URL, pick a
scanned network (or type one manually), enter its password, submit — the
device saves the credentials to NVS and reboots straight into station mode.
No app, no JavaScript, no captive-portal auto-popup (open the URL manually).

To re-enter setup later (e.g. after a password change), **hold BOOT while
powering on**; this is checked right at the start of `app_main()` and skips
straight to the portal regardless of stored credentials.

The old `idf.py menuconfig` dev-SSID fallback (`hardware/networking/wifi`,
`CONFIG_HELIOS_WIFI_DEV_SSID`) still exists but is now effectively unused in
the normal flow — `main.c` only calls `wifi_sta_start()` once credentials
are already confirmed present.

Two of the API-surface risks flagged during the initial skeleton write
turned out to be real and are now fixed in code:

- **`espressif/es8311` uses the legacy `driver/i2c.h` API**
  (`es8311_create(i2c_port_t, uint16_t)`), not the new
  `i2c_master_bus_handle_t`. Since the codec shares its I2C bus/pins with
  the touch controller, `hardware/i2c_bus` and `hardware/touch` were
  switched to the legacy driver too (a bus/port can only be owned by one
  driver generation at a time). See `docs/HARDWARE_REFERENCE.md`.
- **`espressif/button`'s real API** is `iot_button_new_gpio_device()` /
  `iot_button_register_cb(handle, event, event_args, cb, usr_data)` — not
  the `button_config_t.type/gpio_button_config` shape guessed originally.
  `ES8311_ADDRRES_0` (the codec I2C address constant, typo and all) was a
  correct guess.

Panel orientation and the doubled boot logo are confirmed good on hardware.
The startup chime went through two redesigns based on listening to the
real hardware — silent (I2S mono-slot mode) → audible but "sounds like an
ocean liner foghorn" (a 4-note chord beats against itself on a small
speaker) → a sequential rising arpeggio — and the user's verdict on the
arpeggio was still "absolutely awful." Rather than keep iterating blind,
**the automatic chime is disabled on boot** (`main.c` no longer calls it);
`audio_play_startup_tone()` is untouched and still reachable manually from
`/debug/speaker` for whenever the sound design resumes. Touch and the
microphone level test are still completely untried. Full story in
`docs/HARDWARE_REFERENCE.md`.

### The settings app: topbar, sidebar, Home Assistant, debug tools

Once connected to the home network, the screen shows **"Helios Mini /
`http://<device-ip>/`"** permanently (updates itself across reconnects).
Browse to that address from any device on the same network to reach
`networking/settings_server` — a small app with a topbar (Helios logo,
live Wi-Fi/HA connection status icons) and a sidebar (collapses to
icons-only on narrow screens), all inline SVG using real **MDI icons**
(`firmware/networking/settings_server/mdi_icons.h`, extracted from
`@mdi/js` - the same icon set Home Assistant's own frontend uses, no
external requests, no icon font):

- **Network** (`/network`) — Wi-Fi connection status (SSID/signal), and a
  toggle for the `HELIOS-MINI-XXXX` setup AP alongside the existing
  station connection (`wifi_sta_set_setup_ap_enabled()`, `WIFI_MODE_STA`
  <-> `WIFI_MODE_APSTA` at runtime) - lets a second device join and reach
  this same settings app at `http://192.168.4.1/` without disturbing the
  current connection. To actually change *which* network the device
  connects to, hold BOOT at power-on instead (full reprovisioning).
- **Home Assistant** (`/ha`, `/ha/save`, `/ha/test`, `/ha/scan`) — base
  URL + Long-Lived Access Token form (Profile → Security → Long-Lived
  Access Tokens in Home Assistant), saved to NVS. Saving tests the
  connection immediately via `helios/ha_client` (a REST call to
  `<url>/api/`) and shows the result. `/ha/scan` searches mDNS
  (`networking/ha_discovery`) for Home Assistant on the LAN; picking a
  result prefills the URL field, Save still commits it.
- **Display** (`/display`, `/display/save`) — which Home Assistant entity
  feeds each energy ring (solar, grid import, grid export, battery charge,
  battery discharge, home) and each ring's 100% power reference, plus a
  live per-entity status panel: not configured / Home Assistant not set
  up / not tested yet / a real error (not found, unavailable, not
  numeric, unauthorized, unreachable) / OK with the live value. Saving
  tests every filled-in entity immediately against the real Home
  Assistant instance, same as `/ha/save`.
- **Debug** (`/debug` and sub-paths) — live system status
  (uptime/heap/PSRAM/Wi-Fi RSSI) plus buttons to flash the screen, play
  the startup chime, and measure the microphone's peak level. No
  gyroscope test - this board doesn't have one.

Verified live: fetched all four pages over the LAN with `curl` (200s,
each response checked to actually end in `</html>`, not just tag-balance
counted), exercised the AP toggle end-to-end through the running device
with the station connection staying up throughout, and exercised
`/display/save` against the real board with both a deliberately wrong
entity id (came back "no such entity in Home Assistant" — a real 404
round-tripped from the user's Home Assistant instance) and a reset back
to empty. Not yet reviewed by a human actually looking at it in a
browser, or with real entity ids configured.

`helios/ha_client` now also fetches individual entity power values
(`ha_client_get_entity_power()`, used by the energy rings below) on top
of the token-validation call — still no WebSocket client or push
updates, everything is polled.

### The energy rings

Three concentric Apple-Watch-style rings on the round panel — solar / grid
/ battery, outer to inner — plus the home's current consumption as big
text in the middle. `helios/energy_model` polls Home Assistant every 10s
(`ha_client_get_entity_power()` against the 6 entities configured on
`/display`), decides which direction is live for the two-entity rings
(grid import vs. export, battery charge vs. discharge), and pushes the
result to `ui/home`'s `energy_rings_update()`. The screen switches from
the persistent IP notice to the rings the first time a `home` entity gets
configured — there's no way back to the IP notice without rebooting.
Entity model and colors mirror the parent Helios HA card's
`chip-appearance.ts` exactly, including battery being two separate
entities (charge/discharge), not one signed value. Full write-up,
including a chunked-HTTP-response bug this surfaced (empty dynamic
strings were silently truncating pages), in `docs/HARDWARE_REFERENCE.md`.

## Flash / monitor

```sh
source firmware/activate.sh     # sets up the ESP-IDF env (see below)
cd firmware
idf.py -p <port> flash monitor
```

If `<port>` isn't obvious, `ls /dev/tty.usb*` (macOS) after plugging in the
board over USB-C — on this board it shows up as a native USB CDC device
(`/dev/tty.usbmodemXXX`), no separate VCP driver needed.

`idf.py monitor` needs a real interactive terminal (it errors with "Monitor
requires standard input to be attached to TTY" if stdin isn't one — e.g.
when run through automation/an agent's tool sandbox rather than a normal
terminal). In that situation, capture the serial log directly with pyserial
instead: open `/dev/tty.usbmodemXXX` at 115200 baud and read.

No `menuconfig` step needed anymore — on first boot the device starts its
own setup network. Join **`HELIOS-MINI-XXXX`** from a phone, browse to
**`http://192.168.4.1/`**, pick your Wi-Fi network, and submit.

## Requirements

- ESP-IDF **5.5.1**, cloned at `~/esp/esp-idf` — matches Waveshare's own
  reference for this board:
  ```sh
  mkdir -p ~/esp && cd ~/esp
  git clone -b v5.5.1 --recursive --depth 1 --shallow-submodules \
      https://github.com/espressif/esp-idf.git
  cd esp-idf && ./install.sh esp32s3
  ```
- `cmake`, `ninja`, `dfu-util` — `brew install cmake ninja dfu-util`
  (ESP-IDF's own installer does not bundle these on macOS)
- Target: `esp32s3`
- Python 3, as required by ESP-IDF's own `install.sh`

All of the above is already done in this environment; `source
firmware/activate.sh` is enough to pick it back up.

## Build

```sh
source firmware/activate.sh
cd firmware
idf.py set-target esp32s3   # already done; re-running is a no-op
idf.py build
idf.py -p <port> flash monitor
```

The component manager fetches `lvgl/lvgl`, `espressif/esp_lcd_sh8601`,
`espressif/button`, and `espressif/es8311` automatically, pinned to the
verified versions in `firmware/dependencies.lock` (committed; cached copies
land in `managed_components/`, not committed — see `.gitignore`).

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
├── networking/
│   ├── wifi/               STA connect + setup-AP toggle, NVS credentials + dev Kconfig fallback
│   ├── provisioning/       SoftAP + HTTP setup portal (spec Section 16 slice)
│   ├── settings_server/    the settings app (Network/Home Assistant/Display/Debug), mdi_icons.h
│   ├── ha_discovery/       mDNS search for Home Assistant on the LAN
│   └── http_forms/         shared form-decoding helpers (used by every HTTP server above)
├── helios/
│   ├── ha_client/          REST calls: validate a URL + token, fetch one entity's power
│   └── energy_model/       polls Home Assistant every 10s, drives ui/home's rings; entity
│                            + ring-limit config storage (energy_config.h)
├── storage/                NVS string get/set wrapper
├── diagnostics/            periodic heap/PSRAM/uptime log + on-demand status/self-tests
└── ui/
    ├── animations/         boot sequence (spec Section 15) + persistent IP screen, Helios logo asset
    └── home/                energy_rings.h/.c - the three-ring + center-text screen (pure LVGL, no HA)
```

`helios/pairing/`, `networking/discovery/`, `ota/`, and the rest of `ui/`
(solar/battery/consumption/grid as separate screens) are intentionally
still empty — the remaining V0.2/V0.3/V0.4 scope per the roadmap in
`docs/SPEC.md` Section 34. `networking/discovery/` (device *being*
discovered by Home Assistant) and `helios/pairing/` in particular will
likely stay empty — Home Assistant pairing now goes through
`settings_server`'s token entry instead (see above), which doesn't need
either.

## Regenerating the boot logo

The Helios logo used in the boot animation
(`firmware/ui/animations/assets/helios_logo.c`) is generated from
`assets/brand/helios-logo.svg` (pulled from the `Helios` repo) via:

```sh
python3 tools/asset-gen/svg_to_lvgl.py \
    assets/brand/helios-logo.svg 440 helios_logo \
    firmware/ui/animations/assets/helios_logo.c
```

Requires `rsvg-convert` (`brew install librsvg`) and Pillow. Do not hand-edit
the generated `.c` file.
