# Helios Mini — Product Specification

**Project:** Helios Mini
**Status:** Product Definition / V0.1
**Target:** Home Assistant energy monitoring
**Hardware:** Waveshare ESP32-S3-Touch-AMOLED-1.32
**Primary power:** USB-C
**Battery:** Not used in product configuration
**Enclosure:** Custom 3D-printed ASA
**Software:** Custom Helios Mini firmware
**Home Assistant:** Native integration / automatic pairing
**License:** Open-source firmware + hardware documentation + STL

---

## 1. Product Vision

Helios Mini is a compact standalone energy display designed for Home Assistant users.

Its purpose is to provide an immediate visual representation of the Home Assistant Energy Dashboard without requiring a phone, tablet, computer, browser or wall-mounted tablet.

The product must follow the same philosophy as Helios:

> **Connect it, configure it once, and it works.**

The device should feel like a finished consumer product rather than a development board.

The underlying Waveshare board is used as the first hardware platform to accelerate development and validate the product before considering custom hardware.

---

## 2. Product Objectives

### Primary objectives

- Display Home Assistant energy information in real time.
- Automatically connect to Home Assistant after initial setup.
- Require minimal technical knowledge from the user.
- Provide a polished, premium visual interface.
- Use the circular AMOLED display to create a distinctive Helios UI.
- Support swipe navigation.
- Provide animated energy-flow visualizations.
- Provide animated Helios branding at startup.
- Support OTA firmware updates.
- Provide a simple browser-based firmware installation process.
- Provide a fully open-source DIY version.

### Secondary objectives

- Prepare the hardware for future Home Assistant Assist / voice interaction.
- Use the integrated microphone and external speaker.
- Provide notification sounds and UI sounds in a future firmware version.
- Allow future hardware revisions without redesigning the software architecture.

---

## 3. Hardware Platform

### Base board

**Waveshare ESP32-S3-Touch-AMOLED-1.32**

Official specifications:

- ESP32-S3 dual-core Xtensa LX7
- Up to 240 MHz
- 8 MB Flash
- 8 MB PSRAM
- 512 KB SRAM
- 384 KB ROM
- 2.4 GHz Wi-Fi 802.11 b/g/n
- Bluetooth 5 LE
- 1.32" AMOLED
- 466 × 466 pixels
- 16.7M colors
- CO5300 display controller
- CST820 capacitive touch controller
- QSPI display interface
- I²C touch interface
- ES8311 audio codec
- Integrated microphone
- External speaker connector
- USB-C
- PWR button
- BOOT button
- 12-pin expansion connector

Source: <https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.32>

---

## 4. Product Hardware Configuration

### 4.1 Power

The commercial Helios Mini will **not include a battery**.

Power source:

- USB-C
- USB cable included
- No wall charger included

The user supplies power using:

- USB power adapter
- computer
- USB hub
- Home Assistant server / powered USB hub
- compatible USB-C power source

The battery connector on the Waveshare board will remain unused.

#### Reason

Removing the battery:

- simplifies the product;
- reduces BOM cost;
- removes battery shipping considerations;
- removes battery safety considerations;
- simplifies certification;
- eliminates charging management from the user experience;
- makes the device permanently available;
- simplifies enclosure design.

---

## 5. Audio Hardware

The Waveshare board contains:

- ES8311 audio codec
- integrated microphone
- external speaker connector

The supplied Waveshare speaker will be installed inside the Helios Mini enclosure.

### V1 audio functionality

Audio is not a primary feature of V1.
The speaker should nevertheless be physically installed and connected.

Possible V1/V1.x uses:

- startup sound;
- connection sound;
- UI interaction sound;
- error notification;
- Home Assistant connection notification.

### Future functionality

The audio subsystem must be designed so it can later support:

- Home Assistant Assist;
- voice commands;
- spoken responses;
- voice notifications;
- AI assistant interaction.

The enclosure must therefore include an acoustically appropriate speaker cavity / grille.

---

## 6. Enclosure

### Material

Primary material: **ASA**

Reasons:

- good temperature resistance;
- good UV resistance;
- suitable for long-term product use;
- more appropriate than PLA for a commercial enclosure;
- good mechanical properties.

Alternative materials may be documented later.

---

## 7. Enclosure Design Requirements

The enclosure must provide:

- secure mounting of the Waveshare board;
- precise AMOLED display alignment;
- unobstructed touch area;
- USB-C access;
- access to BOOT button;
- access to PWR button if required;
- microphone acoustic opening;
- speaker acoustic opening;
- Wi-Fi antenna clearance;
- internal cable management;
- no pressure on the AMOLED panel;
- no contact between PCB components and enclosure;
- easy assembly;
- repeatable assembly;
- printable geometry without support where reasonably possible.

---

## 8. Enclosure Architecture

Preferred architecture:

### Front shell

Contains:

- circular AMOLED opening;
- front bezel;
- touch surface;
- optional Helios branding;
- status LED/light guide if added later.

### Rear shell

Contains:

- PCB mounting points;
- speaker cavity;
- USB-C opening;
- ventilation/acoustic openings where appropriate;
- assembly screws;
- optional wall/desk mounting system.

---

## 9. Speaker Integration

The enclosure must include a dedicated speaker chamber.

Requirements:

- speaker firmly mounted;
- no rattling;
- no contact with PCB;
- acoustic grille directed toward the user;
- sufficient air volume around speaker;
- cable routed safely;
- connector remains serviceable during assembly.

The exact speaker cavity must be designed from the actual Waveshare-supplied speaker dimensions.

---

## 10. Display

Resolution: **466 × 466 px**
Display: **1.32" AMOLED**

Target UI:

- circular;
- high contrast;
- dark background;
- Helios visual identity;
- animated graphics;
- large numerical values;
- minimal text;
- optimized for glanceability.

The AMOLED display is capable of approximately 500 cd/m² brightness and has a 10000:1 contrast ratio according to Waveshare's product specifications.

---

## 11. Touch Interaction

Touch controller: **CST820**

Primary interaction:

### Horizontal swipe

Swipe left: Next page
Swipe right: Previous page

Touch interactions should be designed around short, reliable gestures.
The firmware should reject accidental micro-swipes and avoid changing pages from unintended touches.

Future gestures may include:

- tap;
- long press;
- double tap.

---

## 12. Main UI

The UI is specifically designed for the circular display.
It must not simply reproduce the desktop Helios interface at a smaller scale.

The UI should be designed around:

- central numerical values;
- circular energy flows;
- animated arcs;
- radial indicators;
- minimal labels;
- strong color coding.

---

## 13. Dashboard Pages

Initial target pages:

### Page 1 — Energy Flow

Displays:

- photovoltaic production;
- home consumption;
- battery flow;
- grid import;
- grid export.

This is the primary Helios Mini screen.

### Page 2 — Solar

Displays:

- current PV power;
- daily PV production;
- optional historical information;
- solar animation.

### Page 3 — Battery

Displays:

- battery state of charge;
- battery power;
- charging / discharging state;
- optional battery animation.

### Page 4 — Home

Displays:

- current home consumption;
- daily consumption;
- optional secondary metrics.

### Page 5 — Grid

Displays:

- current import;
- current export;
- current grid state;
- optional daily totals.

---

## 14. Energy Visualization

The device should use a circular animated indicator around the perimeter of the AMOLED.
The perimeter acts as a visual status indicator.

Example states:

### Solar production

Color: yellow / orange
Animation: energy flowing inward/outward depending on the visualization.

### Battery charging

Color: green / cyan
Animation: flow toward battery.

### Battery discharging

Color: blue / purple
Animation: flow away from battery.

### Grid import

Color: orange / red
Animation: flow from grid toward house.

### Grid export

Color: green / cyan
Animation: flow from house toward grid.

The exact colors should be aligned with the existing Helios visual identity.

---

## 15. Startup Animation

On boot:

1. Black screen.
2. Small central light.
3. Circular animation.
4. Helios logo appears.
5. Logo animation completes.
6. Wi-Fi connection state appears.
7. Home Assistant connection state appears.
8. Main energy screen loads.

The startup animation should remain short.

Target: **< 3 seconds**, excluding network connection time.

---

## 16. Wi-Fi Provisioning

First boot must provide a simple Wi-Fi setup process.

Preferred experience:

1. Device boots.
2. Device enters provisioning mode.
3. User connects to Helios Mini using a phone.
4. User selects their Wi-Fi network.
5. User enters the Wi-Fi password.
6. Device connects.
7. Device stores credentials.
8. Device discovers Home Assistant.
9. Device starts pairing.

The user must not need to install:

- ESP-IDF;
- Arduino IDE;
- Python;
- drivers;
- command-line tools.

---

## 17. Home Assistant Integration

The device must integrate directly with Home Assistant.
The Home Assistant Energy Dashboard remains the conceptual source of truth.
The firmware must not scrape Lovelace HTML.

Preferred communication: **Home Assistant WebSocket API**

The firmware should establish a persistent authenticated connection.

Conceptual architecture:

```
Home Assistant
      │
      │ WebSocket
      ▼
Helios Mini
      │
      ├── Solar
      ├── Battery
      ├── Home
      ├── Grid Import
      └── Grid Export
```

---

## 18. Home Assistant Pairing

The final user experience should not require the user to manually copy a long-lived Home Assistant token.

Preferred solution:

- dedicated Helios Mini Home Assistant integration;
- device discovery;
- user confirmation;
- secure authentication;
- automatic configuration.

Target experience:

```
Helios Mini detected

        HELIOS-MINI-XXXX

            [ ADD ]
```

After pairing:

```
Home Assistant connected ✓
Energy Dashboard connected ✓
```

---

## 19. Firmware Architecture

Preferred framework: **ESP-IDF**

Reason:

- production-grade ESP32 development;
- FreeRTOS;
- Wi-Fi control;
- OTA;
- NVS;
- USB;
- audio;
- display;
- touch;
- robust task management.

Potential UI framework: **LVGL**

Alternative rendering technologies may be evaluated during development if performance or memory usage requires it.

---

## 20. Firmware Components

Suggested architecture:

```
firmware/
├── main/
├── hardware/
│   ├── display/
│   ├── touch/
│   ├── audio/
│   ├── buttons/
│   └── power/
│
├── ui/
│   ├── home/
│   ├── solar/
│   ├── battery/
│   ├── consumption/
│   ├── grid/
│   └── animations/
│
├── helios/
│   ├── energy_model/
│   ├── ha_client/
│   └── pairing/
│
├── networking/
│   ├── wifi/
│   ├── provisioning/
│   └── discovery/
│
├── ota/
├── storage/
└── diagnostics/
```

---

## 21. OTA Updates

After initial setup, firmware updates should be performed over Wi-Fi.

Requirements:

- signed/verified firmware;
- version number;
- update progress;
- automatic reboot;
- rollback/recovery mechanism if possible;
- recovery firmware mode.

Target user experience:

```
Helios Mini

New firmware available

v1.2.0

[ UPDATE ]
```

No USB connection should be required for normal updates.

---

## 22. Browser-Based Firmware Installer

A dedicated web installer should be provided.

Target URL:

```
https://<helios-domain>/mini/install
```

Target workflow:

```
Connect USB-C
      ↓
Open installer
      ↓
Install Helios Mini
      ↓
Wait
      ↓
Disconnect
      ↓
Boot
      ↓
Configure Wi-Fi
```

The user should not need to manually manipulate firmware files.

---

## 23. Factory Firmware

Commercial units must ship with:

- production firmware;
- correct device identity;
- factory test mode;
- OTA enabled;
- diagnostics;
- version information.

Factory test should verify:

- display;
- touch;
- Wi-Fi;
- audio output;
- microphone;
- buttons;
- USB;
- firmware integrity.

---

## 24. Diagnostics

A hidden diagnostics page should be available.

Possible information:

```
Helios Mini

Firmware: 1.0.0
Hardware: WS-1.32
Wi-Fi: Connected
RSSI: -54 dBm
Home Assistant: Connected
WebSocket: Connected
Uptime: 4h 32m
Free heap: xxxx
PSRAM: xxxx
Display: OK
Touch: OK
Audio: OK
```

This is important for customer support.

---

## 25. Commercial Product Configuration

Included:

- Waveshare ESP32-S3-Touch-AMOLED-1.32
- Waveshare speaker
- custom ASA enclosure
- USB-C cable
- assembled and tested device
- Helios Mini firmware
- Home Assistant compatibility
- documentation

Not included:

- USB wall charger
- battery
- Home Assistant server
- microSD card

---

## 26. DIY Edition

All major components and documentation should be available for users who want to build the product themselves.

Provide:

- firmware source;
- firmware releases;
- STL files;
- STEP files if available;
- BOM;
- wiring diagram;
- assembly guide;
- flashing guide;
- Home Assistant integration;
- troubleshooting guide.

The DIY version should not be intentionally crippled.

---

## 27. Open-Source Repository

Suggested repository:

```
helios-mini-display
```

Structure:

```
helios-mini-display/
│
├── firmware/
├── home-assistant/
├── hardware/
├── enclosure/
│   ├── stl/
│   ├── step/
│   └── source/
├── installer/
├── assets/
├── docs/
├── tools/
├── LICENSE
├── README.md
└── CHANGELOG.md
```

> **Repo note:** implemented as `Helios-Mini` on GitHub, matching the capitalization of the sibling `Helios` / `Helios-Forecast` repositories.

---

## 28. 3D Printing

Primary material: **ASA**

Recommended documentation:

- nozzle diameter;
- layer height;
- wall count;
- infill;
- supports;
- bed temperature;
- enclosure temperature;
- print orientation;
- recommended tolerances.

STL variants:

```
helios-mini-front.stl
helios-mini-back.stl
helios-mini-speaker-grille.stl
helios-mini-stand.stl
```

Parametric source:

```
helios-mini.scad
```

---

## 29. Enclosure Design Philosophy

The enclosure should look like a commercial product.

Avoid:

- visible development-board aesthetics;
- exposed PCB;
- unnecessary screws on the front;
- large openings;
- generic rectangular electronics-box appearance.

Desired appearance:

- minimal;
- rounded;
- premium;
- compact;
- unmistakably Helios.

The circular AMOLED should dominate the front face.

---

## 30. Product Dimensions

Final external dimensions are TBD.

They must be derived from:

1. Waveshare PCB dimensions.
2. AMOLED panel dimensions.
3. PCB mounting holes.
4. Connector locations.
5. Speaker dimensions.
6. USB-C position.
7. Button positions.
8. Required internal clearances.
9. ASA printing tolerances.

A mechanical reference model of the actual Waveshare board should be created before final enclosure design.

---

## 31. Mechanical Tolerances

Initial 3D-printing targets:

- press-fit features: test and tune;
- screw holes: printer-dependent;
- snap fits: avoid for V1 unless tested;
- PCB clearance: minimum safety margin around components;
- display clearance: no mechanical pressure on panel;
- USB-C opening: include generous insertion clearance.

Exact tolerances must be validated on the target printer.

---

## 32. Future Features

Potential V2/V3 features:

- Home Assistant Assist;
- voice interaction;
- spoken energy information;
- configurable notifications;
- alarms;
- weather display;
- electricity price;
- energy autonomy;
- CO₂ information;
- presence-aware display;
- brightness automation;
- ambient light sensor;
- custom dashboards;
- additional Home Assistant entities.

---

## 33. Future Custom PCB

The Waveshare board is the V1 development platform.
If Helios Mini reaches sufficient production volume, evaluate a custom Helios Mini PCB.

Goals:

- reduce BOM;
- reduce assembly cost;
- optimize enclosure;
- integrate speaker;
- integrate USB-C;
- simplify production;
- potentially improve audio;
- improve power management;
- simplify certification.

Do not develop custom hardware before validating product-market fit.

---

## 34. Product Versions

### V0.1 — Hardware Bring-Up

- display
- touch
- Wi-Fi
- speaker
- microphone
- buttons
- USB

### V0.2 — Home Assistant

- Wi-Fi provisioning
- HA discovery
- authentication
- WebSocket
- energy data

### V0.3 — Helios UI

- energy flow
- solar
- battery
- home
- grid
- swipe
- animations

### V0.4 — Production

- OTA
- installer
- diagnostics
- factory test
- enclosure
- documentation

### V1.0 — Commercial Release

- stable firmware
- stable HA integration
- production enclosure
- complete documentation
- open-source DIY release

---

## 35. Definition of Done — V1

Helios Mini V1 is considered production-ready when a non-technical Home Assistant user can:

1. Unbox the device.
2. Connect USB-C.
3. Power it on.
4. Configure Wi-Fi.
5. Pair Home Assistant.
6. Have the device automatically identify the energy configuration.
7. See solar production.
8. See home consumption.
9. See battery state and flow.
10. See grid import/export.
11. Swipe between screens.
12. Receive firmware updates over Wi-Fi.

No computer, terminal, developer tools or manual firmware flashing should be required.

---

## 36. Commercial Positioning

Product: **Helios Mini**

Positioning: The tiny Home Assistant energy display.

Primary value proposition: Your energy dashboard, always visible.

Key selling points:

- Home Assistant native;
- plug & play;
- beautiful AMOLED display;
- compact circular design;
- real-time energy visualization;
- OTA updates;
- open source;
- DIY-friendly;
- future voice assistant support.

---

## 37. Business Model

### DIY

Free.

Includes:

- source code;
- firmware;
- STL;
- documentation.

### Kit

Potential future product.

Includes:

- Waveshare board;
- speaker;
- cable;
- optional components.

### Fully assembled

Target retail price: **~129 € TTC**

Initial Kickstarter target pricing may be lower to reward early supporters.

Target:

- Early Bird: ~99–109 €
- Kickstarter: ~119 €
- Retail: ~129 €

Final pricing depends on:

- Waveshare bulk pricing;
- enclosure production cost;
- packaging;
- assembly time;
- payment fees;
- taxes;
- certification;
- support;
- shipping.

---

## 38. Kickstarter Strategy

Kickstarter should only be considered after:

- functional prototype;
- 5–10 beta units;
- stable firmware;
- stable Home Assistant integration;
- validated enclosure;
- known BOM;
- real production cost;
- initial user feedback.

Potential campaign goals:

### Base goal

First production batch.

### Stretch goal

Custom PCB.

### Future stretch goal

Advanced audio / Home Assistant Assist integration.

---

## 39. Regulatory / Production Requirements

Before commercial sale in the EU, evaluate applicable requirements including:

- CE;
- Radio Equipment Directive (RED);
- EMC;
- RoHS;
- WEEE / DEEE;
- General Product Safety Regulation;
- technical documentation;
- declaration of conformity;
- manufacturer information;
- traceability;
- user documentation;
- packaging requirements.

The fact that the underlying Waveshare board may itself have compliance documentation does not automatically mean the complete Helios Mini product is compliant in its final commercial configuration.

---

## 40. Current MVP Scope

### MUST HAVE

- ESP32-S3
- AMOLED 466×466
- touch
- Wi-Fi
- USB-C
- Home Assistant connection
- automatic Wi-Fi provisioning
- automatic HA pairing
- Energy Dashboard data
- PV
- battery
- home consumption
- import/export
- swipe navigation
- Helios animations
- startup animation
- OTA
- factory firmware
- ASA enclosure
- integrated speaker

### SHOULD HAVE

- UI sounds
- diagnostics
- browser firmware installer
- automatic recovery
- OTA rollback

### FUTURE

- Home Assistant Assist
- voice interaction
- spoken responses
- advanced notifications
- custom PCB
- ambient light sensor
- additional sensors

---

## 41. Core Product Principle

Helios Mini should never feel like:

> "an ESP32 development board running a custom program."

It should feel like:

> **"a real Home Assistant product made by Helios."**

The technology should disappear behind the user experience.

> **Plug it in.
> Connect Wi-Fi.
> Connect Home Assistant.
> See your energy.**

---

## 42. Official Hardware Reference

Waveshare documentation: <https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.32>

Waveshare product page: <https://www.waveshare.com/product/esp32-s3-touch-amoled-1.32.htm>

The hardware specifications in this document should be revalidated against the exact production revision before manufacturing.
