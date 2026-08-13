# Helios Mini Firmware

ESP-IDF firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.32, targeting the
**V0.1 — Hardware Bring-Up** milestone from `docs/SPEC.md` (Section 34):
display, touch, Wi-Fi, buttons, and USB — plus, ahead of
schedule, the Wi-Fi provisioning slice of **V0.2** (spec Section 16): a
SoftAP + browser setup page, no more hardcoded dev credentials required.

**Home Assistant pairing deliberately does not follow spec Sections 17-18**
(discovery + a dedicated custom integration). Instead, once connected to the
home network, the device runs its own small settings server (reachable at
the IP permanently shown on screen) that can find Home Assistant on the LAN
via mDNS, takes a URL + a Long-Lived Access Token, and tests the connection
immediately — simpler to build and to use, at the cost of one manual step
(generating the token in HA) instead of a one-tap pairing button. The same
server also has a `/debug` page with a live status snapshot, a screenshot
download, and a factory reset. See "Home Assistant settings" below. The Helios UI, OTA, and
full diagnostics come in later versions.

## Status: running on hardware, Wi-Fi setup verified end-to-end

Builds with **zero errors and zero warnings** against ESP-IDF 5.5.1
(`idf.py set-target esp32s3 && idf.py build`, verified with the actual
resolved managed-component versions: `lvgl/lvgl 9.5.0`,
`espressif/esp_lcd_sh8601 2.0.1~1`, `espressif/button 4.2.0`).

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

`hardware/i2c_bus` and `hardware/touch` use the legacy `driver/i2c.h` API
(see `docs/HARDWARE_REFERENCE.md`); migrating to `driver/i2c_master.h` is a
deferred candidate. `espressif/button`'s real API is
`iot_button_new_gpio_device()` / `iot_button_register_cb(...)`.

Panel orientation and the doubled boot logo are confirmed good on hardware.
Audio was removed from the product: poor sound quality on the small
speaker, and dropping it frees enclosure space. No I2S/codec code remains.

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
- **Display** (`/display`, `/display/save`, `/display/rescan`) — a
  read-only status panel for the energy rings, auto-discovered from Home
  Assistant's own Energy Dashboard configuration (Settings → Dashboards →
  Energy) — nothing typed here at all. Each row shows: not set up in the
  Energy Dashboard / Home Assistant not set up on Helios Mini / waiting
  for a first live update / a live power value (flagged stale if it stops
  updating). "Rescan" re-reads Home Assistant's config on demand (after
  the user edits it there). Below that, the ring-limits form (4 numbers,
  Helios Mini's own display preference, saved via `/display/save`).
- **Debug** (`/debug`) — live system status (uptime/heap/PSRAM/Wi-Fi RSSI),
  a screenshot download, and a factory reset.

Verified live: fetched all four pages over the LAN with `curl` (200s,
each response checked to actually end in `</html>`, not just tag-balance
counted), exercised the AP toggle end-to-end through the running device
with the station connection staying up throughout, and confirmed
`/display` auto-discovers real power entities from the user's own Home
Assistant Energy Dashboard - solar, grid import, and grid export all went
live within seconds of connecting, with real wattages. Not yet reviewed
by a human actually looking at it in a browser, or on the physical round
panel itself.

### The energy rings

Three concentric Apple-Watch-style rings on the round panel — solar / grid
/ battery, outer to inner — plus the home's current consumption as big
text in the middle. `helios/ha_ws` connects to Home Assistant's websocket
API, reads its Energy Dashboard configuration, and subscribes to live
updates for each ring's linked **power** entity — the same kind of entity
the parent Helios HA card's own power chips read. Home Assistant's Energy
Dashboard also always has a cumulative *energy* (kWh) statistic alongside
that power entity, for HA's own billing/history graphs — Helios Mini
never uses it, not even as a fallback: a ring with no power entity linked
is simply reported not configured, full stop. `helios/energy_model` turns
`ha_ws`'s live status into ring percentages/colors every 3s, decides
which direction is active for the two-entity rings (grid import vs.
export, battery charge vs. discharge), and pushes the result to
`ui/home`'s `energy_rings_update()`. The screen switches from the
persistent IP notice to the rings the first time the Energy Dashboard has
*any* source configured — there's no way back to the IP notice without
rebooting. Entity model and colors mirror the parent Helios HA card's
`chip-appearance.ts` exactly, including battery being two separate
entities (charge/discharge), not one signed value.

Getting to "power-only, zero typing" took a few real corrections against
the user's actual Home Assistant instance along the way — a schema
assumption that didn't match their real setup, a first attempt that still
derived power from energy deltas when a live power entity was available,
and a stack overflow from summing multiple sources per ring. Full
write-up in `docs/HARDWARE_REFERENCE.md`.

Ring updates animate into place (a real lerp via LVGL's own animation
timer, not a hard jump), have no background track — only the colored
fill is drawn, and even at 0%/not-configured it still shows a small
rounded dot marking the ring's start rather than vanishing outright. A
small MDI house icon sits above the center number instead of a "home
consumption" text label. Home consumption itself mirrors the parent
Helios card's own formula verbatim (`consumptionLoad()` in
`helios/src/core/energy.ts`): `production + gridImport - gridExport -
netBattery`, clamped at 0. How power numbers are displayed everywhere
(rings and `/display`'s status panel) is a single shared preference —
`/display`'s form now also has a W/kW toggle and a 0-3 decimal-places
slider (`energy_config.h`'s `energy_format_t`, default W/1 decimal).

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
`espressif/button`, `espressif/mdns`, and `espressif/esp_websocket_client`
automatically, pinned to the verified
versions in `firmware/dependencies.lock` (committed; cached copies land
in `managed_components/`, not committed — see `.gitignore`).

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
│   ├── i2c_bus/            I2C bus (CST820 touch)
│   ├── touch/              CST820 driver
│   ├── display/            QSPI CO5300 bring-up + LVGL v9 port
│   └── buttons/            BOOT/PWR via espressif/button
├── networking/
│   ├── wifi/               STA connect + setup-AP toggle, NVS credentials + dev Kconfig fallback
│   ├── provisioning/       SoftAP + HTTP setup portal (spec Section 16 slice)
│   ├── settings_server/    the settings app (Network/Home Assistant/Display/Debug), mdi_icons.h
│   ├── ha_discovery/       mDNS search for Home Assistant on the LAN
│   └── http_forms/         shared form-decoding helpers (used by every HTTP server above)
├── helios/
│   ├── ha_client/          REST calls: validate a URL + token, fetch one entity's power
│   ├── ha_ws/               Home Assistant websocket client: auth, Energy Dashboard
│   │                        discovery, live power-entity subscriptions (power only,
│   │                        never energy/kWh - see docs/HARDWARE_REFERENCE.md)
│   └── energy_model/       turns helios/ha_ws's live status into ring percentages/colors
│                            every 3s, drives ui/home; ring-limit config storage
│                            (energy_config.h - Helios Mini's own display preference,
│                            not discovered from Home Assistant)
├── storage/                NVS string get/set wrapper
├── diagnostics/            periodic heap/PSRAM/uptime log + on-demand status/self-tests
└── ui/
    ├── animations/         boot sequence (spec Section 15) + persistent IP screen, Helios logo asset
    └── home/                energy_rings.h/.c - the three-ring + center-icon-and-text
                               screen (pure LVGL, no HA), assets/mdi_home_icon.c (generated)
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

The boot animation draws the Helios logo in piece by piece - its 12
sun-flame rays, then the central disc - rather than fading in one flat
image. `assets/brand/helios-logo.svg` (pulled from the `Helios` repo)
happens to already be one `<path>` with 13 separate "M...Z" subpaths (the
12 rays + the disc), which is exactly what makes this possible without
hand-splitting the artwork. Regenerate
`firmware/ui/animations/assets/helios_logo_pieces.c/.h` via:

```sh
python3 tools/asset-gen/svg_pieces_to_lvgl.py \
    assets/brand/helios-logo.svg 440 helios_logo_piece \
    firmware/ui/animations/assets/helios_logo_pieces.c \
    firmware/ui/animations/assets/helios_logo_pieces.h
```

Each subpath is rasterized at the same px-per-SVG-unit density as a full
440px render of the whole logo, then cropped to its own bounding box - a
single flame is a small fraction of the canvas, so the 13 pieces together
end up smaller in flash than one full 440x440 image would be. Requires
`rsvg-convert` (`brew install librsvg`) and Pillow. Do not hand-edit either
generated file.

`tools/asset-gen/svg_to_lvgl.py` (single flat image, no piece-splitting)
is still there and still used for smaller standalone icons - the MDI house
icon on the energy rings screen
(`firmware/ui/home/assets/mdi_home_icon.c`) is generated from
`assets/icons/mdi-home.svg` the same way:

```sh
python3 tools/asset-gen/svg_to_lvgl.py \
    assets/icons/mdi-home.svg 40 mdi_home_icon \
    firmware/ui/home/assets/mdi_home_icon.c
```
