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
- V0.1 Hardware Bring-Up firmware skeleton (ESP-IDF, unbuilt): power latch,
  shared I2C bus, CST820 touch, QSPI CO5300 display + LVGL v9 port,
  BOOT/PWR buttons, I2S + ES8311 audio bring-up, Wi-Fi station connect,
  NVS storage wrapper, diagnostics logging, and the Section 15 boot
  animation using the Helios logo. See `firmware/README.md` for build
  instructions and known unverified API surfaces.
