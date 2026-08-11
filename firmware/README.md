# Helios Mini Firmware

ESP-IDF firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.32, targeting the
**V0.1 — Hardware Bring-Up** milestone from `docs/SPEC.md` (Section 34):
display, touch, Wi-Fi, speaker, microphone, buttons, USB — plus, ahead of
schedule, the Wi-Fi provisioning slice of **V0.2** (spec Section 16): a
SoftAP + browser setup page, no more hardcoded dev credentials required.

**Home Assistant pairing deliberately does not follow spec Sections 17-18**
(discovery + a dedicated custom integration). Instead, once connected to the
home network, the device runs its own small settings server (reachable at
the IP permanently shown on screen) where the user pastes an HA URL and a
Long-Lived Access Token — simpler to build and to use, at the cost of one
manual step (generating the token in HA) instead of a one-tap pairing
button. See "Home Assistant settings" below. The Helios UI, OTA, and
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

Remaining open items, still unverified because they need eyes/ears on the
physical device (not just a clean serial log) — see
`docs/HARDWARE_REFERENCE.md`: the panel orientation fix, the doubled boot
logo, the startup tone actually being audible, and touch.

### Persistent on-screen IP + Home Assistant settings

Once connected to the home network, the screen shows **"Helios Mini /
`http://<device-ip>/`"** permanently (updates itself across reconnects).
Browse to that address from any device on the same network to reach
`networking/settings_server`: a form for the Home Assistant base URL and a
Long-Lived Access Token (Profile → Security → Long-Lived Access Tokens in
Home Assistant), saved to NVS. **This only saves the settings** — nothing
yet reads them back and actually talks to Home Assistant (no WebSocket
client, no energy data). That's the next piece of work.

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
│   ├── wifi/               STA connect, NVS credentials + dev Kconfig fallback
│   ├── provisioning/       SoftAP + HTTP setup portal (spec Section 16 slice)
│   ├── settings_server/    post-connect HTTP server: HA URL + token -> NVS
│   └── http_forms/         shared form-decoding helpers (used by both servers above)
├── storage/                NVS string get/set wrapper
├── diagnostics/            periodic heap/PSRAM/uptime log
└── ui/animations/          boot sequence (spec Section 15) + persistent IP screen, Helios logo asset
```

`helios/`, `networking/discovery/`, `ota/`, and the rest of `ui/`
(home/solar/battery/consumption/grid) are intentionally still empty — the
remaining V0.2/V0.3/V0.4 scope per the roadmap in `docs/SPEC.md` Section 34.
`networking/discovery/` in particular will likely stay empty — Home
Assistant pairing now goes through `settings_server`'s token entry instead
of device discovery (see above), which doesn't need it.

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
