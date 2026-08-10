# Helios Mini

The tiny Home Assistant energy display.

Helios Mini is a compact, standalone circular display that shows your Home Assistant Energy Dashboard in real time — solar production, home consumption, battery flow, and grid import/export — with no phone, tablet, or wall-mounted tablet required.

Connect it, configure it once, and it works.

It's the little sibling of [Helios](https://github.com/ReikanYsora/Helios), the 2.5D solar card for Home Assistant, and pairs with [Helios-Forecast](https://github.com/ReikanYsora/Helios-Forecast) for solar production forecasting.

## Status

**V0.1 — Hardware Bring-Up, builds clean, not yet flashed.** Firmware for
display, touch, Wi-Fi, buttons, and audio is written in
[`firmware/`](firmware/), ported from Waveshare's own reference firmware for
this exact board, and compiles with zero errors/warnings against ESP-IDF
5.5.1. Board arrives soon — first flash/monitor is next. See
[`firmware/README.md`](firmware/README.md) for the day-of checklist,
[`docs/SPEC.md`](docs/SPEC.md) for the full product specification, and
[`docs/HARDWARE_REFERENCE.md`](docs/HARDWARE_REFERENCE.md) for the verified
pin mapping and build notes.

## Hardware

- **Base board:** Waveshare ESP32-S3-Touch-AMOLED-1.32 (ESP32-S3, 466×466 circular AMOLED, CST820 touch, ES8311 audio codec)
- **Power:** USB-C only, no battery in the commercial configuration
- **Enclosure:** custom 3D-printed ASA, open-sourced as STL/STEP

## Repository structure

```
Helios-Mini/
├── firmware/        ESP-IDF firmware (display, touch, audio, Wi-Fi, HA client, OTA, UI)
├── home-assistant/  Home Assistant integration for discovery/pairing
├── hardware/         Mechanical reference of the Waveshare board, connectors, dimensions
├── enclosure/        3D-printable enclosure
│   ├── stl/          Ready-to-print STL files
│   ├── step/          STEP files for CAD editing
│   └── source/       Parametric source (OpenSCAD)
├── installer/        Browser-based firmware installer (WebSerial/ESP Web Tools)
├── assets/           Branding, renders, product images
├── docs/             Specification and technical documentation
├── tools/            Dev/build/flash helper scripts
└── CHANGELOG.md
```

## License

Open-source firmware, hardware documentation, and enclosure STL/STEP files, licensed under [GPL-3.0](LICENSE).

## Related projects

- [Helios](https://github.com/ReikanYsora/Helios) — the original 2.5D Home Assistant solar card
- [Helios-Forecast](https://github.com/ReikanYsora/Helios-Forecast) — self-learning solar production forecast integration
