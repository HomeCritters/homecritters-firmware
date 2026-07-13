# HomeCritters — firmware

A **desk pet + smart speaker** for your home: an original pixel-art ferret
named Leon living in a magic forest, on a Xiaozhi/Spotpear **Ball V2**
(ESP32-S3, round 240×240 GC9A01 touch display, ES8311 audio codec).

Feed him, pet him, play mini-games with him — and when music plays, he throws
a party. Works fully standalone, and becomes a first-class
[Home Assistant](https://www.home-assistant.io/) device (sensors, buttons,
media player for TTS + [Music Assistant](https://www.music-assistant.io/) +
AirPlay) with the companion
[homecritters-ha-plugin](https://github.com/HomeCritters/homecritters-ha-plugin).

![platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![framework](https://img.shields.io/badge/framework-Arduino%20%2B%20PlatformIO-orange)
![HA](https://img.shields.io/badge/Home%20Assistant-integration-41BDF5)

| The pet | Party mode 🪩 | Voice ring 🔵 |
|---|---|---|
| ![pet](docs/screenshots/pet-day.png) | ![party](docs/screenshots/party-mode.png) | ![voice](docs/screenshots/voice-ring.png) |

## Features

### 🦦 A living pet

Four stats (hunger, energy, joy, hygiene) decay over time; his mood shows in
the header and on the RGB LED. Leon wanders, jumps, eats, burrows into the
ground and sleeps — animated from an original Aseprite sprite sheet. The
forest follows the **real time of day** (NTP): sunny day, golden sunset, and
a starry night with a lit cabin window and fireflies.

| Day | Night + idle clock | Config menu |
|---|---|---|
| ![day](docs/screenshots/pet-day.png) | ![night](docs/screenshots/night-clock.png) | ![menu](docs/screenshots/config-menu.png) |

Leave him alone for a while and the HUD gives way to an **idle clock**
(NTP-synced, 12/24h, timezone auto-detected by the web portal).

### 🎮 Three mini-games

Playable on the touch screen — or from your phone, with the web portal
acting as a game controller.

| Games menu | Jump! | Bolinha (fetch) | Genius |
|---|---|---|---|
| ![games](docs/screenshots/games-menu.png) | ![jump](docs/screenshots/game-jump.png) | ![fetch](docs/screenshots/game-fetch.png) | ![genius](docs/screenshots/game-genius.png) |

- **Jump!** — doodle-jump style: springs, moving and crumbling platforms,
  parallax clouds, NVS high score.
- **Bolinha** — fetch: throw the tennis ball with a swipe, Leon chases it
  down with real physics and brings it back.
- **Genius** — Simon-says on the round bezel: four color arcs, the RGB LED
  and an authentic tone per color.

### 📱 Web portal (no app needed)

The device serves a React portal from flash at `http://critter.local` —
live stats over WebSocket, remote actions, a live mirror of Leon's
animation, settings, screenshots, and game controllers.

| Portal | As a game controller |
|---|---|
| ![portal](docs/screenshots/web-portal.png) | ![controller](docs/screenshots/web-portal-game.png) |

### 🔊 Media player

A Voice PE-style audio pipeline (network reader task → 1 MB PSRAM ring →
decoder) plays **FLAC / MP3 / WAV** over http — web radio, Home Assistant
TTS, Music Assistant tracks, and AirPlay (via Music Assistant's AirPlay
receiver). Format is auto-detected from the stream header.

And it's a show:

- 🪩 **Party mode** when music plays: forced night theme, mirrored disco
  ball, random laser pulses, a color-shifting dance floor, smoke machines,
  thumping speaker cabinets — and Leon dancing through all of it while the
  LED cycles a rainbow.
- 🔵 **Voice ring** when the assistant speaks (TTS): an Alexa-style cyan
  ring sweeps the round bezel, with the LED pulsing in sync.

### 🏠 Home Assistant

With the [companion integration](https://github.com/HomeCritters/homecritters-ha-plugin)
(HACS): auto-discovery via zeroconf, stat/mood/battery sensors, action
buttons, sleep & clock switches, brightness sliders and a `media_player`
entity for TTS and Music Assistant (set the player codec to **MP3 or FLAC**
— ALAC is not supported).

## Hardware

Spotpear/Xiaozhi **Ball V2**:

| Part | Chip |
|---|---|
| MCU | ESP32-S3 (16 MB flash, 8 MB octal PSRAM) |
| Display | GC9A01 round 240×240, SPI |
| Touch | CST816 (dedicated I2C bus) |
| Audio | ES8311 codec + speaker amp, I2S |
| Extras | WS2812 RGB LED, battery ADC, BOOT button |

Full commented pinout in [`include/pins.h`](include/pins.h) (single source of
truth), based on [RealDeco/xiaozhi-esphome](https://github.com/RealDeco/xiaozhi-esphome).

> ⚠️ Hard-won lesson: route I2S **MCLK to GPIO16** (its real pin). The audio
> library's silent default (GPIO0) radiates an ~11 MHz clock next to the
> antenna and desenses WiFi whenever audio plays.

## Building & flashing

```bash
pio run                 # build
pio run -t upload       # build and flash (USB-C; hold BOOT if no port shows up)
pio device monitor      # serial log, 115200 baud
```

First boot opens a **WiFi captive portal** (AP "HomeCritters") for network
setup. After that the device announces itself as `critter.local`.

## Regenerating assets

Assets are embedded as generated headers (no filesystem upload step):

```bash
# Ferret animation frames (from the Aseprite sheet in assets/)
python3 assets/aseprite_to_frames.py

# Sounds (any MP3 -> PROGMEM header)
python3 assets/mp3_to_header.py <file.mp3> <symbol> include/sounds/<name>.h

# Web portal (React) -> gzipped single file in flash
cd web && npm install && npm run build && cd ..
python3 assets/web_to_header.py
```

## Project layout

```
src/            firmware modules (Pet, Renderer, FerretActor, AudioPlayer,
                WebPortal, Clock, InputController, StatusLed, Battery, games)
src/audio/      media pipeline: AudioReader (esp_http_client) -> StreamRing
                (1MB PSRAM SPSC) -> decoder task
include/        pinout, game config, theme, UI layout + generated assets
assets/         sprite sheet + asset pipeline scripts
web/            React portal (Vite + Ant Design), embedded via web_index.h
tools/          hwshot.py (screen -> PNG over serial) + serial console
```

UI strings are in Portuguese (pt-BR); code and comments in English.
See `CLAUDE.md` for full architecture notes.

## License

MIT
