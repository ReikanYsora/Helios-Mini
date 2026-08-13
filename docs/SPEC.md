# Helios Mini - Product Specification

**Product:** Helios Mini, the tiny Home Assistant energy display.
**Status:** running on real hardware; feature set validated, in a
reorganization/hardening pass toward a finished, showable prototype.
**Hardware:** Waveshare ESP32-S3-Touch-AMOLED-1.32, USB-C powered, no battery.
**Software:** custom ESP-IDF firmware (LVGL v9), open source.
**Home Assistant:** WebSocket API, sources read straight from the Energy Dashboard.
**License:** GPL-3.0 (firmware + hardware documentation + enclosure STL/STEP).

This is the engineering specification. Milestone history lives in
`CHANGELOG.md`; verified pin mapping and hardware notes live in
`docs/HARDWARE_REFERENCE.md`.

---

## 1. Product vision

Helios Mini shows your Home Assistant Energy Dashboard in real time on a
compact circular display, with no phone, tablet, browser, or wall-mounted
tablet in the loop. It is the little sibling of the Helios 2.5D solar card.

The guiding principle, same as Helios:

> Connect it, configure it once, and it works.

It must feel like a finished consumer product, not a development board
running a program. The technology disappears behind the experience:

> Plug it in. Connect Wi-Fi. Connect Home Assistant. See your energy.

The Waveshare board is the first hardware platform, used to validate the
product before any custom PCB is considered.

---

## 2. Hardware platform

**Waveshare ESP32-S3-Touch-AMOLED-1.32:**

- ESP32-S3 dual-core LX7, up to 240 MHz, 8 MB flash, 8 MB PSRAM
- 1.32" AMOLED, 466 x 466, 16.7M colors, CO5300 controller, QSPI
- CST820 capacitive touch (I2C)
- Wi-Fi 802.11 b/g/n, Bluetooth 5 LE
- USB-C, PWR button, BOOT button

The board also carries an ES8311 audio codec, microphone, and speaker
connector. Audio is not used: sound quality on the small speaker was poor,
and dropping it frees enclosure space. No audio code is built.

Hardware specs must be revalidated against the exact production revision
before manufacturing. Reference:
<https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.32>.

---

## 3. Power

USB-C only. No battery in the product configuration (the board's battery
connector stays unused). Rationale: simpler product, lower BOM, no battery
shipping/safety/certification burden, permanently-on device, simpler
enclosure. A USB-C cable is included; a wall adapter is not.

---

## 4. Enclosure

Custom 3D-printed **ASA** (temperature/UV resistance, suitable for long-term
product use). Two shells:

- **Front:** circular AMOLED opening, bezel, touch surface, optional Helios
  branding. The round display dominates the face.
- **Rear:** PCB mounting, USB-C opening, ventilation where appropriate,
  assembly screws, optional wall/desk mount.

Requirements: secure board mounting, precise display alignment, unobstructed
touch, USB-C and BOOT access, Wi-Fi antenna clearance, no mechanical pressure
on the panel, no PCB-to-shell contact, repeatable assembly, print-friendly
geometry. No speaker cavity (no audio).

Appearance: minimal, rounded, premium, compact, unmistakably Helios; no
exposed PCB, no front screws, no generic-electronics-box look.

Final dimensions and tolerances are derived from the real board (PCB, panel,
mounting holes, connectors, USB-C position, clearances) and validated on the
target printer. STL + STEP + parametric source are open-sourced.

---

## 5. Display and UI principles

466 x 466 AMOLED, ~500 cd/m2, ~10000:1 contrast. The UI is designed for the
round display, not a shrunk desktop Helios:

- circular energy flows, animated arcs, radial indicators
- large central values, minimal text, strong color coding
- dark background, high contrast, glanceable
- colors aligned with the Helios visual identity

---

## 6. Touch interaction

CST820 capacitive touch. Primary gesture is horizontal swipe (next/previous
page); the general view also swipes up to a status screen. Gestures are kept
short and reliable; accidental micro-swipes must not change pages. Tap /
long-press / double-tap are reserved for later.

---

## 7. Screens

A swipeable set of pages (LVGL tileview), horizontal axis plus one vertical
neighbor:

- **General view (always shown):** four concentric Apple-Watch-style rings -
  irradiance / solar / grid / battery, outer to inner - with home
  consumption as the central number under a house icon. Each ring shows its
  color and current fill; a ring with no configured source is not drawn.
- **Solar, Grid, Battery, Irradiance (hero pages):** one dedicated page per
  source, borrowing Helios's own HUD-chip language (a pill chip with icon +
  value, fed by a dashed lead line with a moving bead whose speed tracks the
  live reading). Each dedicated page is individually toggleable.
- **Consumption:** the home total plus a per-contributor breakdown (solar,
  grid import/export, battery charge/discharge) - the one thing the compact
  center number cannot show.
- **Status (swipe up):** the device's IP address, plus a setup QR code (see
  Onboarding).

Which dedicated pages appear is configured in the settings app; the general
view is mandatory.

---

## 8. Energy data model

The Home Assistant Energy Dashboard is the single source of truth. The
firmware never scrapes Lovelace and never uses cumulative energy (kWh)
statistics.

- Each ring's source is **auto-discovered** from the Energy Dashboard
  configuration (Settings -> Dashboards -> Energy). Zero manual entity entry.
- Only the linked **live power (W)** entity of each source is used - the same
  entity the Helios card's own power chips read. A source with no power
  entity linked is simply reported "not configured".
- A ring can be fed by several power entities (e.g. multiple strings); every
  one found is subscribed and summed.
- Grid and battery each show whichever direction is currently active (import
  vs export, charge vs discharge), decided by the larger above-noise reading.
- Home consumption mirrors the Helios card's own formula verbatim
  (`consumptionLoad()`): `production + gridImport - gridExport - netBattery`,
  `netBattery = charge - discharge`, clamped at 0. Battery is two separate
  entities, not one signed value.
- Colors mirror the Helios card's `chip-appearance.ts`.

Live updates arrive over the Home Assistant WebSocket API (authenticated with
the stored token); a REST bootstrap gives each entity a first value
immediately.

---

## 9. Irradiance

Independent of the Energy Dashboard. Ground-horizontal irradiance (W/m2) is
computed from the home's location (read from Home Assistant's own config) and
live cloud cover from Open-Meteo, using the same blend Helios uses:
`effective_cloud = low + 0.6*mid + 0.2*high`, capped at 100. The ring's 100%
reference is 1000 W/m2 (STC). Shows "waiting" until location, weather, and a
synced clock all resolve.

---

## 10. MQTT bridge (optional)

When a broker is configured, Helios Mini exposes on-device controls and
diagnostics to Home Assistant as auto-discovered MQTT entities: screen
brightness (control), Wi-Fi signal/connected, free heap/PSRAM, uptime, and
ground irradiance / cloud cover. Off by default; a bad broker configuration
must never destabilize the device (bounded reconnects, heap guard).

---

## 11. Onboarding

First boot has no stored credentials, so the device brings up its own open
Wi-Fi network `HELIOS-MINI-XXXX` and a plain HTTP setup server. The user
joins it from a phone/laptop, picks their Wi-Fi network, enters the password,
and the device saves it and connects.

Home Assistant is then linked from the settings app (reachable at the IP the
device shows on screen): the user points Helios Mini at their instance (mDNS
scan pre-fills the URL) and pastes a **Long-Lived Access Token** (created in
Home Assistant: profile -> Security -> Long-Lived Access Tokens). Saving
tests the connection immediately.

**Planned onboarding refinement (next milestone):** make setup fully plug and
play, matching Helios's finish.

- First boot shows a **QR code on the screen** so the user never types an IP;
  scanning it opens the setup page directly.
- A single minimalist flow: pick Wi-Fi + password, then an automatic Home
  Assistant scan shows the found instance, then paste the token, each step
  clearly guided.
- On success the device tears down its setup AP and joins the configured
  network. If that join fails, nothing is persisted and setup restarts from
  scratch on the next boot.
- The permanent status screen carries the same QR to reopen setup later.

Token entry stays **copy-paste** for now. Scanning the token via the phone
camera is deferred: browser camera access requires a secure (HTTPS) context,
which the plain-HTTP setup page does not provide, and Home Assistant does not
emit a token QR today. Candidates for later: a self-signed HTTPS setup
server, or a dedicated Helios Mini Home Assistant integration.

Re-entering full setup later: hold BOOT while powering on.

---

## 12. Startup sequence

On boot: black screen, then the Helios logo draws itself in (its sun-flame
rays fade in one by one, then the central disc), then Wi-Fi and Home
Assistant connection state, then the energy screen. Target under 3 seconds
excluding network time. No startup sound. The panel must not show any white
flash before real content is on screen.

---

## 13. Firmware architecture

ESP-IDF (FreeRTOS, Wi-Fi, OTA, NVS, USB), LVGL v9 for the UI. Components live
nested under `hardware/`, `networking/`, `helios/`, `ui/` rather than a flat
directory:

```
firmware/
  main/                 app_main(): boot sequencing only
  hardware/
    board_config/       pin map (header-only, single source of truth)
    power/              power latch (asserted early)
    i2c_bus/            I2C bus (CST820 touch)
    touch/              CST820 driver
    display/            QSPI CO5300 bring-up + LVGL v9 port
    buttons/            BOOT/PWR
  networking/
    wifi/               STA connect + setup-AP toggle
    provisioning/       SoftAP + HTTP setup portal
    settings_server/    the settings app (Network / Home Assistant / Display / MQTT / Debug)
    ha_discovery/       mDNS search for Home Assistant on the LAN
    http_forms/         shared form-decoding helpers
  helios/
    ha_client/          REST: validate URL+token, fetch instance info / entity power
    ha_ws/              WebSocket client: auth, Energy Dashboard discovery, live power subs
    energy_model/       maps ha_ws status to ring fill/color, drives ui; ring-limit config
    irradiance_model/   location + Open-Meteo cloud cover
    mqtt_bridge/        optional HA MQTT-discovery bridge
  storage/              NVS string get/set wrapper
  diagnostics/          heap/PSRAM/uptime/RSSI status
  ui/
    animations/         boot sequence + logo asset
    screens/            the energy screens (general + hero + consumption + status)
```

Components consume the narrowest dependency they need; the UI layer knows
nothing about Home Assistant (it renders a plain snapshot pushed by
`energy_model`).

---

## 14. OTA updates (future)

After setup, updates run over Wi-Fi: signed/verified firmware, version
number, progress, automatic reboot, and a recovery/rollback path. Requires an
OTA-capable partition table (two app slots). The current app is small enough
to fit two slots in 8 MB flash comfortably. A browser-based installer
(WebSerial / ESP Web Tools) provides the first flash without dev tools.

---

## 15. Diagnostics

A hidden status view and the settings app's `/debug` page report firmware
version, Wi-Fi/HA/WebSocket link state, uptime, free heap/PSRAM, and Wi-Fi
RSSI, plus a live screenshot download and a factory reset. This is the
first-line data for support.

---

## 16. Roadmap

- **Done:** hardware bring-up (display, touch, Wi-Fi, buttons), Wi-Fi
  provisioning, settings app, Home Assistant WebSocket + Energy Dashboard
  auto-discovery, the energy screens, irradiance, MQTT bridge.
- **Next:** the plug-and-play onboarding refinement (Section 11), dashboard
  polish, remove the boot white flash, and the internal reorganization /
  performance and memory hardening pass.
- **Later:** OTA + browser installer + recovery, production enclosure, factory
  firmware/test.
- **Future (V2+):** Home Assistant Assist / voice (would require re-adding
  audio hardware), electricity price, autonomy, ambient light / presence.

---

## 17. Commercialization

Open-source firmware, hardware documentation, and enclosure files, so anyone
can build a **DIY** unit (not intentionally crippled): firmware source +
releases, STL/STEP, BOM, wiring, assembly and flashing guides.

Beyond DIY: a possible **kit** (board + cable + parts) and a **fully
assembled** unit (target retail ~129 EUR TTC), with lower early-bird pricing.
A Kickstarter is considered only after a functional prototype, a handful of
beta units, stable firmware/integration, a validated enclosure, a known BOM,
and real user feedback.

Selling assembled units in the EU requires evaluating CE / RED / EMC / RoHS /
WEEE / GPSR compliance and the associated documentation; the board's own
compliance does not automatically cover the finished product.

---

## 18. Definition of done (V1)

A non-technical Home Assistant user can unbox the device, power it over USB-C,
configure Wi-Fi, pair Home Assistant, and then see solar production, home
consumption, battery state/flow, and grid import/export, swiping between
screens, receiving firmware updates over Wi-Fi - with no computer, terminal,
or manual flashing at any point.
