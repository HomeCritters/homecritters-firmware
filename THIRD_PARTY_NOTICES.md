# Third-party notices

The HomeCritters firmware — the source in `src/`, `include/` (hand-written
headers), `web/`, `tools/` and `assets/`, plus the original Leon pixel-art
sprite sheet — is licensed under the [MIT License](LICENSE).

It builds on third-party work that keeps its own licenses. Nothing below is
vendored into this repository: the C++ libraries are fetched by PlatformIO at
build time (`lib_deps` in `platformio.ini`) and the JavaScript packages by npm
(`web/package.json`). Their license texts ship with those packages.

## Firmware libraries (linked into the binary)

| Library | Version | License |
|---|---|---|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) — display + touch | ^1.1.16 | MIT AND BSD-2-Clause |
| [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) — RGB LED | ^1.12.3 | **LGPL-3.0** |
| [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) — FLAC/MP3/WAV decoders | ^1.9.7 | **GPL-3.0** |
| [WiFiManager](https://github.com/tzapu/WiFiManager) — captive portal | ^2.0.17 | MIT |
| [QRCode](https://github.com/ricmoo/QRCode) — portal QR code | ^0.0.1 | MIT |
| [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets) — WS server | ^2.6.1 | **LGPL-2.1** |

Platform code underneath: the [Arduino core for
ESP32](https://github.com/espressif/arduino-esp32) (LGPL-2.1) and
[ESP-IDF](https://github.com/espressif/esp-idf) (Apache-2.0), including
mbedTLS (Apache-2.0), used for the HMAC-SHA256 handshake and the TLS
certificate bundle.

### ⚠️ If you distribute a compiled binary

**The audio decoder library is GPL-3.0.** This project's own source stays MIT
(MIT is GPL-compatible), and building the firmware for yourself carries no
obligation. But a **compiled `.bin` that anyone distributes** — a GitHub
release artifact, a preflashed device, a web flasher image — is a combined work
with GPL-3.0 code in it, and must be distributed under GPL-3.0 terms: the
complete corresponding source of everything linked in, offered to whoever
receives the binary.

In practice that means one of:

- keep shipping **source only** (what this repository does today), or
- publish binaries and honour GPL-3.0 for the combined work, or
- replace ESP8266Audio with a permissively licensed decoder first.

The LGPL libraries (NeoPixel, arduinoWebSockets, Arduino-ESP32 core) are
statically linked too, which under LGPL requires that recipients be able to
relink the binary against a modified version of those libraries — again
satisfied trivially while distribution is source-only.

## Web portal packages

[React](https://react.dev/) (MIT), [Ant Design](https://ant.design/) (MIT),
[Vite](https://vite.dev/) (MIT), `@vitejs/plugin-react` (MIT) and
[vite-plugin-singlefile](https://github.com/richardtallent/vite-plugin-singlefile)
(MIT). The SHA-256/HMAC implementation in `web/src/hmac.js` is written for this
project (the portal is served over plain `http://`, where `crypto.subtle` is
unavailable) and is covered by this project's MIT license.

## External services

- [Open-Meteo](https://open-meteo.com/) — weather data, free for non-commercial
  use, no API key, [CC BY 4.0](https://open-meteo.com/en/license). The firmware
  calls it directly over verified TLS. Geocoding for the city search runs in the
  browser against the same API.
- NTP servers (`pool.ntp.org` and fallbacks) for the clock.

## Hardware and reference material

The pinout in `include/pins.h` was derived from
[RealDeco/xiaozhi-esphome](https://github.com/RealDeco/xiaozhi-esphome)
(`Ball_v2.yaml`). The board itself ("Ball V2") is a Spotpear/Xiaozhi product;
this project is not affiliated with, endorsed by, or supported by Spotpear,
Xiaozhi, Home Assistant, Nabu Casa, or Amazon.

"Alexa" is used only as the default **wake word phrase** recognised locally by
[openWakeWord](https://github.com/dscripka/openWakeWord) running on your own
Home Assistant instance. No Amazon software or service is involved, and the
wake word is configurable.

## Bundled media assets

- **Leon's sprite sheet** (`assets/ferret-sprite-sheet.png` + `.json`, and the
  generated `include/ferret_anim.h` / `include/ferret_game.h`) is original art
  made for this project and is covered by the MIT license above.
- **Sound effects** (`include/sounds/*.h`) are MP3/WAV clips embedded as
  PROGMEM byte arrays. They were collected from public sound-effect sites
  (mostly [MyInstants](https://www.myinstants.com/)) during development, and
  **their individual copyright status has not been cleared**. They are included
  for personal, non-commercial use of this project. If you redistribute this
  firmware — and especially if you build anything commercial on it — audit
  `include/sounds/` and replace any clip you do not have the right to use.
  Contributors: only add audio you know you may redistribute under MIT
  (CC0/public-domain sources, or clips you made yourself).
