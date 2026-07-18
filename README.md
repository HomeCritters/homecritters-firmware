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

| Day | Golden hour | Night + idle clock | Config menu |
|---|---|---|---|
| ![day](docs/screenshots/pet-day.png) | ![sunset](docs/screenshots/pet-sunset.png) | ![night](docs/screenshots/night-clock.png) | ![menu](docs/screenshots/config-menu.png) |

Leave him alone for a while and the HUD gives way to an **idle clock**
(NTP-synced, 12/24h, timezone auto-detected by the web portal).

### 🌦️ Real weather

The scene follows the **actual weather** at your place (free Open-Meteo API,
fetched by the firmware itself over verified TLS — no Home Assistant, no API
key). **Every WMO condition has its own look** — palette tint, cloud cover,
precipitation density and speed, and effects like visible lightning bolts
with a thunder clap, hail pellets and icy freezing-rain crystals. Ambient
sounds too: rain patter and forest wind, played sparsely and never over
other audio. **Swipe up from the bottom** for the forecast screen — today
big (icon, current temp, condition, hi/lo + humidity) plus the next four
days (icon, hi/lo, chance of rain). Set your city once in the web portal
(the browser does the geocoding search); if the HA integration is connected
it gifts the home location automatically.

<table>
<tr>
<td align="center"><img src="docs/screenshots/wx-clear.png" width="150"/><br><sub>Clear (0)</sub></td>
<td align="center"><img src="docs/screenshots/wx-mclear.png" width="150"/><br><sub>Mainly clear (1)</sub></td>
<td align="center"><img src="docs/screenshots/wx-partly.png" width="150"/><br><sub>Partly cloudy (2)</sub></td>
<td align="center"><img src="docs/screenshots/wx-cloudy.png" width="150"/><br><sub>Overcast (3)</sub></td>
<td align="center"><img src="docs/screenshots/wx-fog.png" width="150"/><br><sub>Fog (45/48)</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/screenshots/wx-drizzle.png" width="150"/><br><sub>Drizzle (51–55)</sub></td>
<td align="center"><img src="docs/screenshots/wx-rainy.png" width="150"/><br><sub>Rain (61–65)</sub></td>
<td align="center"><img src="docs/screenshots/wx-pouring.png" width="150"/><br><sub>Showers (80–82)</sub></td>
<td align="center"><img src="docs/screenshots/wx-frain.png" width="150"/><br><sub>Freezing rain (56/57/66/67)</sub></td>
<td align="center"><img src="docs/screenshots/wx-snow.png" width="150"/><br><sub>Snow (71–75)</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/screenshots/wx-grains.png" width="150"/><br><sub>Snow grains (77)</sub></td>
<td align="center"><img src="docs/screenshots/wx-snowshower.png" width="150"/><br><sub>Snow showers (85/86)</sub></td>
<td align="center"><img src="docs/screenshots/wx-storm.png" width="150"/><br><sub>Thunderstorm (95)</sub></td>
<td align="center"><img src="docs/screenshots/wx-hail.png" width="150"/><br><sub>Hail (96/99)</sub></td>
<td align="center"><img src="docs/screenshots/wx-clear-night.png" width="150"/><br><sub>Clear, at night ✨</sub></td>
</tr>
</table>

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
  thumping speaker cabinets — Leon dancing through all of it while the RGB
  LED **flashes to the actual beat** (live PCM envelope analysis).
- 🔵 **Voice rings** for the assistant (below).

### 🎙️ Voice assistant

**Just say "Alexa"** — always-on wake word, no button needed. The mic
(ES8311 ADC, 16 kHz) streams continuously to Home Assistant, where
openWakeWord (local, private) listens for the wake word; on a hit the
Assist pipeline takes over — STT, your conversation agent, and the spoken
reply plays back through the speaker. If the assistant answers with a
follow-up question, the mic reopens by itself so you can just reply. Every
phase gets its own ring around the round bezel, plus iOS-style earcons.
**Hold BOOT to push-to-talk** at any time (the button beats the wake word).

A **quick BOOT tap mutes the microphone** (Echo-style privacy: nothing
leaves the device — a hard gate at the source) — a crossed-mic icon shows
on screen. When the assistant can actually hear you (device streaming
*and* the HA pipeline confirmed it's consuming the audio), a small **cyan
mic** shows at the top; if the pipeline gets unstable it blinks — honest,
end-to-end status you can debug at a glance.

| Listening | Thinking | Speaking | Mic muted |
|---|---|---|---|
| ![listen](docs/screenshots/voice-listen.png) | ![think](docs/screenshots/voice-think.png) | ![speak](docs/screenshots/voice-ring.png) | ![mute](docs/screenshots/mute-mic.png) |

### 🔒 Pairing & security

TV-style pairing: when an app asks to connect, a **random 6-digit PIN pops
on the screen by itself**; typing it (portal OTP boxes / HA config flow)
hands the client a long-lived credential. Every WebSocket client must
authenticate before *anything* — state, commands and especially the mic —
and screenshots require the token too. The Security menu lists the devices
connected right now and can **revoke everyone** with a two-tap confirm
(clients re-pair by PIN; HA lands in its reauth flow automatically).

| Pairing PIN | Devices + revoke |
|---|---|
| ![pin](docs/screenshots/pairing-pin.png) | ![devices](docs/screenshots/security-devices.png) |

### 🏠 Home Assistant

With the [companion integration](https://github.com/HomeCritters/homecritters-ha-plugin)
(HACS): auto-discovery via zeroconf + PIN pairing, stat/mood/battery
sensors, action buttons, switches (sleep, **night mode** with configurable
sleep/wake sounds — great for schedule automations —, **microphone mute**,
idle clock), brightness sliders, every device setting (pet name, timezone,
time/date formats, timeouts) organized in Controls / Configuration /
Diagnostics sections, a `media_player` entity for TTS and Music Assistant
(set the player codec to **MP3 or FLAC** — ALAC is not supported) and an
`assist_satellite` with always-on wake word + push-to-talk voice.

There's also a **"Casa" panel on the device itself**: pull the left edge of
the pet screen to open a paginated grid of your HA entities — lights,
switches, fans and locks as one-tap toggles, temperature/humidity/
illuminance and presence sensors read-only. Pick (and drag-order) the
entities in the integration's Configure dialog.

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
