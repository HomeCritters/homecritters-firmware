# Ferret Ball — desk tamagotchi

Firmware that turns the Xiaozhi/Spotpear **Ball V2** (ESP32-S3, round GC9A01
240x240 touch display) into a desk virtual pet: an original pixel-art ferret
living in a magic forest, with sound, a web portal and an idle clock mode.

![platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![framework](https://img.shields.io/badge/framework-Arduino%20%2B%20PlatformIO-orange)

## Features

- **Animated ferret** (Aseprite sprite sheet): idle, wandering left/right,
  jumping, eating, burrowing (dig → disappear → emerge) and sleeping.
- **4 stats** decaying over time: hunger, energy, joy, hygiene — with mood
  shown in the header and mirrored on the RGB LED.
- **Magic forest scene** with day/night switching (sun ↔ moon + stars, lit
  cabin window at night) driven by the sleep state.
- **Sound** through the ES8311 codec: per-action effects + snoring while
  asleep, MP3s embedded in flash, volume with a perceptual curve.
- **Touch UI**: action buttons on an arc, pet-anywhere, swipe-down config
  menu (volume, WiFi setup, portal QR code).
- **Web portal (React + Ant Design)** served from flash: live state over
  WebSocket, remote actions, pet renaming, volume and clock settings —
  including a live mirror of the ferret's current animation and position.
- **WiFi** via WiFiManager captive portal, `ferret.local` mDNS hostname.
- **Idle clock mode**: after a configurable idle time the HUD gives way to
  an NTP-synced clock (timezone + 12/24h configurable from the portal).
- State persisted to NVS (survives reboots).

## Building & flashing

```bash
pio run                 # build
pio run -t upload       # build and flash (USB-C; hold BOOT if no port shows up)
pio device monitor      # serial log, 115200 baud
```

## Regenerating assets

Assets are embedded as generated headers (no filesystem upload step):

```bash
# Ferret animation frames (from the Aseprite sheet in assets/)
python3 assets/aseprite_to_frames.py

# Sounds (any MP3 -> PROGMEM header)
python3 assets/mp3_to_header.py <file.mp3> <symbol> include/<name>.h

# Web portal (React) -> gzipped single file in flash
cd web && npm install && npm run build && cd ..
python3 assets/web_to_header.py
```

## Project layout

```
src/            firmware modules (Pet, Renderer, FerretActor, AudioPlayer,
                WebPortal, Clock, InputController, StatusLed, Battery)
include/        pinout, game config, theme, UI layout + generated assets
assets/         sprite sheet + asset pipeline scripts
web/            React portal (Vite + Ant Design), embedded via web_index.h
```

See `CLAUDE.md` for the full architecture notes and hardware pinout source.
