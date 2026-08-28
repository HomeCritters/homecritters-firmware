# Installing HomeCritters from scratch

Everything you need to go from a brand-new **Spotpear/Xiaozhi Ball V2** (or a
fresh clone of this repo) to a living ferret on your desk.

## What you need

| Thing | Notes |
|---|---|
| Spotpear/Xiaozhi **Ball V2** | ESP32-S3, round 240×240 touch display, ES8311 audio ([product page](https://spotpear.com/shop/ESP32-S3-AI-1.28-inch-Round-LCD-Display-Screen-Xiaozhi-Ai-Chatbox-Voice-Sphere-Robot-Ball-V2.html)) |
| USB-C **data** cable | Charge-only cables won't enumerate a serial port |
| Python 3.9+ | For PlatformIO and the asset/debug scripts |
| [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/) | `pipx install platformio` (or the VS Code extension) |
| A 2.4 GHz WiFi network | The ESP32-S3 has no 5 GHz radio |

Node.js is **not** required for a normal install — all generated assets
(sprites, sounds, the React portal) are committed as headers. You only need it
if you change things under `web/` (see [Regenerating assets](../README.md#regenerating-assets)).

## 1. Get the code and build

```bash
git clone https://github.com/HomeCritters/homecritters-firmware.git
cd homecritters-firmware
pio run          # first build downloads the toolchain + libraries (~a few min)
```

## 2. Flash the board

Plug the Ball V2 in via USB-C.

```bash
pio run -t upload
```

- **No serial port showing up?** Hold the **BOOT** button while plugging the
  cable in (the S3 enters the ROM bootloader), then run the upload again.
- **Coming from the stock/other firmware?** Do a one-time full erase first so
  leftover NVS data can't confuse anything:

  ```bash
  pio run -t erase && pio run -t upload
  ```

- **More than one device connected?** Point the upload at a specific port:

  ```bash
  pio run -t upload --upload-port /dev/cu.usbmodemXXXX
  ```

Watch it boot (optional):

```bash
pio device monitor   # 115200 baud. Note: opening the port resets the board.
```

## 3. First boot: WiFi

With no saved credentials the device opens a **captive portal**:

1. On your phone, join the WiFi network **"HomeCritters"**.
2. The setup page pops up (or browse to `192.168.4.1`).
3. Pick your home network and enter the password.

The device reboots onto your network and announces itself over mDNS as
`critter-XXYYZZ.local` (unique per device, from the MAC). The IP is also
printed on the serial log and shown in the on-screen config menu
(swipe down → Conexao).

## 4. Pair the web portal

Open `http://critter-XXYYZZ.local/` (or the IP) in a browser on the same
network:

1. The portal asks to pair and a **6-digit PIN appears on the device screen**.
2. Type it in the browser. Done — the browser holds its own credential from
   here on (no password to remember).

From the portal you can now:

- **Rename your pet** (the name other devices see on the walkie-talkie),
- set the **weather city** (type to search; geocoding runs in the browser),
- confirm the **timezone/clock** (auto-detected from the browser),
- set the **birthday** (cake day 🎂),
- watch the **live screen mirror** — and control the device by touching it.

Everything else works out of the box: stats, mini-games (pull the right edge),
real weather on the scene, festive dates, idle clock.

## 5. Optional: Home Assistant

Install the companion integration
[homecritters-ha-plugin](https://github.com/HomeCritters/homecritters-ha-plugin)
via HACS (custom repository → integration). The device is discovered by
zeroconf automatically; pairing is the same on-screen PIN flow. You get
sensors, buttons, switches, a `media_player` (TTS / Music Assistant — codec
**MP3 or FLAC**, not ALAC) and an `assist_satellite` for the always-on
"Alexa" wake word. The device-side "Casa" panel (pull the left edge) shows
your chosen HA entities.

## 6. Optional: a second device (walkie-talkie 📻)

Repeat steps 2–4 on another Ball V2 and give it a different pet name. On the
same LAN the devices **discover each other automatically** — open the games
menu (pull the right edge) → **Walkie** → your friend's name is on the list.
Hold the big yellow button to talk; broadcast to everyone with "Todos".

## Troubleshooting

| Symptom | Fix |
|---|---|
| Upload can't find a port | Data-capable cable + hold **BOOT** while plugging in |
| `pio: command not found` | Ensure PlatformIO is on your PATH (`pipx ensurepath`) or use the full path to the binary |
| Portal loads but won't pair | The PIN expires in 90 s / 3 attempts — reopen the portal to mint a new one |
| Serial monitor shows a reboot when you connect | Expected: opening the USB serial port auto-resets the S3 |
| No audio during WiFi activity, or streaming stutters | You changed the I2S pinout: MCLK **must** stay on GPIO16 (see the warning in the [README](../README.md#hardware)) |
| Weather never appears | Set a city in the portal; the device needs internet (TLS to open-meteo.com) |

## Where things live

- Full architecture notes: [`CLAUDE.md`](../CLAUDE.md)
- Pinout (single source of truth): [`include/pins.h`](../include/pins.h)
- Debug tools (screenshots over serial/HTTP, console): [`tools/README.md`](../tools/README.md)
- Contributing: [`CONTRIBUTING.md`](../CONTRIBUTING.md)
