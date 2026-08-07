# Third-party notices

The HomeCritters firmware — the source in `src/`, `include/` (hand-written
headers), `web/`, `tools/` and the scripts in `assets/` — is licensed under the
[PolyForm Noncommercial License 1.0.0](LICENSE): use, modify and share freely
for any noncommercial purpose.

The artwork and the sound effects were made by other people and keep their own
terms — see [Artwork](#artwork-the-ferret) and [Sound effects](#sound-effects).

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

**The audio decoder library is GPL-3.0.** Building and flashing the firmware
for yourself carries no obligation. But a **compiled `.bin`** — a GitHub
release artifact, a preflashed device, a web flasher image — is a combined
work with GPL-3.0 code in it, and the GPL does not allow combining with
noncommercial-only terms in a distributed binary. So this project is
**source-distributed by design**: clone it, `pio run`, flash your own. Don't
publish compiled binaries.

The LGPL libraries (NeoPixel, arduinoWebSockets, Arduino-ESP32 core) are
statically linked too; source-only distribution satisfies their conditions as
well.

## Web portal packages

[React](https://react.dev/) (MIT), [Ant Design](https://ant.design/) (MIT),
[Vite](https://vite.dev/) (MIT), `@vitejs/plugin-react` (MIT) and
[vite-plugin-singlefile](https://github.com/richardtallent/vite-plugin-singlefile)
(MIT). The SHA-256/HMAC implementation in `web/src/hmac.js` is written for this
project (the portal is served over plain `http://`, where `crypto.subtle` is
unavailable) and is covered by this project's license.

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

## Artwork: the ferret

Every frame of the little guy comes from:

> ### 🦦 [2D Pixel Art Ferret Sprites](https://elthen.itch.io/2d-pixel-art-ferret-sprites)
> by **Elthen's Pixel Art Shop** ([itch.io](https://elthen.itch.io/) ·
> [Patreon](https://www.patreon.com/elthen))

Huge thanks to Elthen — this whole project exists because that sprite sheet was
charming enough to build a device around. If you like him, go
[support the artist](https://www.patreon.com/elthen).

This covers `assets/ferret-sprite-sheet.png` / `.json` and the generated
`include/ferret_anim.h` and `include/ferret_game.h` (the same pixels, re-encoded
as RGB565 PROGMEM arrays). The art is used under Elthen's own terms — see the
[asset page](https://elthen.itch.io/2d-pixel-art-ferret-sprites) — so this
project's license covers the code, not the sprites.

## Sound effects

`include/sounds/*.h` are short MP3/WAV clips embedded as PROGMEM byte arrays,
collected from public sound-effect sites (mostly
[MyInstants](https://www.myinstants.com/)). Credit goes to whoever made each
one; they're here for the fun of a desk pet that goes *boing*.
