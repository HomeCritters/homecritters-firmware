# Debug tools

Talk to the firmware's serial console (`src/main.cpp` → `dispatchSerialCmd`)
to drive the device and grab screenshots — handy for validating UI without
eyes on the hardware. Use the PlatformIO python (has `pyserial` + `Pillow`):

```bash
PY=~/.platformio/penv/bin/python

# screenshot the current screen -> PNG
$PY tools/hwshot.py -o shot.png

# navigate first, then shoot (‑‑cmd is a console command)
$PY tools/hwshot.py --cmd games        -o games.png
$PY tools/hwshot.py --cmd "menu:luz"   -o luz.png
$PY tools/hwshot.py --cmd "stats:80,20,50,10" --cmd pet -o low.png

# just send commands (no screenshot)
$PY tools/console.py games
$PY tools/console.py "stats:80,20,50,10" pet
$PY tools/console.py help
```

The port is auto-detected (`/dev/cu.usbmodem*`), override with `--port`.

> **Caveat:** opening the USB serial port RESETS the ESP32-S3 (auto-reset can't
> be fully suppressed here). So a serial screenshot reboots the board and shows
> a freshly-booted screen — it will NOT capture live idle state like the NTP
> clock or the time-of-day theme (those need WiFi+sync+idle, lost on reboot).
> To capture the **live** state, use the **web** screenshot instead:
>
> ```bash
> curl -s http://ferret.local/shot.bmp -o shot.bmp   # no reset; live state
> ```
>
> or the **📸 button in the React portal**. Serial screenshots are best for
> quickly checking navigated UI (menus/games), not live runtime state.

## Console commands

`shot` · `pet` · `games` · `doodle` · `ball` ·
`menu[:main|audio|luz|qr]` · `feed` · `pat` · `clean` · `sleep` ·
`vol:N` · `led:N` · `scr:N` (0..100) · `stats:H,E,J,Hy` (0..100 each) · `help`

## How the screenshot works

`shot` makes the Renderer stream the canvas buffer over Serial: a text header
`@@SHOT <w> <h>\n`, then `w*h*2` raw **big-endian** RGB565 bytes (the canvas
renders with `setSwapBytes`). `hwshot.py` reassembles that into a PNG.
