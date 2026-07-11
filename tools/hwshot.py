#!/usr/bin/env python3
"""Grab a screenshot of the Ball V2 hardware screen over serial.

The firmware's serial console (src/main.cpp) streams the current canvas as raw
RGB565 when it receives "shot". This tool optionally drives the device to a
screen first (--cmd), then captures and saves a PNG.

Run with the PlatformIO python (has pyserial + Pillow):
    ~/.platformio/penv/bin/python tools/hwshot.py -o shot.png
    ~/.platformio/penv/bin/python tools/hwshot.py --cmd games -o games.png
    ~/.platformio/penv/bin/python tools/hwshot.py --cmd "menu:luz" --cmd shot ...

--cmd may be repeated; each is sent (in order) with a short settle delay before
the screenshot is taken.
"""
import argparse
import glob
import sys
import time

import serial
from PIL import Image


def find_port() -> str:
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyUSB*") +
                   glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("No serial port found (looked for /dev/cu.usbmodem*, ttyUSB*, ttyACM*).")
    return ports[0]


def fill_until(ser, buf: bytearray, cond, timeout: float):
    """Read into `buf` until cond(buf) is truthy; return cond's value."""
    deadline = time.time() + timeout
    while True:
        r = cond(buf)
        if r:
            return r
        if time.time() > deadline:
            raise TimeoutError("timed out waiting for serial data")
        chunk = ser.read(4096)
        if chunk:
            buf.extend(chunk)


def rgb565_to_png(data: bytes, w: int, h: int, path: str):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for i in range(w * h):
        # The canvas stores RGB565 big-endian (it renders with setSwapBytes).
        v = (data[2 * i] << 8) | data[2 * i + 1]
        r = ((v >> 11) & 0x1F) << 3
        g = ((v >> 5) & 0x3F) << 2
        b = (v & 0x1F) << 3
        px[i % w, i // w] = (r | (r >> 5), g | (g >> 6), b | (b >> 5))
    img.save(path)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="hwshot.png")
    ap.add_argument("-p", "--port", default=None)
    ap.add_argument("--cmd", action="append", default=[],
                    help="console command to send before the shot (repeatable)")
    ap.add_argument("--settle", type=float, default=0.5,
                    help="seconds to wait after the last --cmd before capturing")
    args = ap.parse_args()

    port = args.port or find_port()
    # dtr/rts low so opening the port doesn't reset the board (keeps its state).
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.dtr = False
    ser.rts = False
    ser.timeout = 0.1
    ser.open()
    time.sleep(0.2)
    ser.reset_input_buffer()

    for c in args.cmd:
        ser.write((c + "\n").encode())
        ser.flush()
        time.sleep(0.25)
    if args.cmd:
        time.sleep(args.settle)

    ser.reset_input_buffer()
    ser.write(b"shot\n")
    ser.flush()

    # One persistent buffer so bytes after the header aren't lost.
    buf = bytearray()

    def header_ready(b):
        m = b.find(b"@@SHOT ")
        if m < 0:
            return None
        nl = b.find(b"\n", m)
        return (m, nl) if nl >= 0 else None

    m, nl = fill_until(ser, buf, header_ready, 5.0)
    w, h = (int(x) for x in buf[m + 7 : nl].split())
    start = nl + 1
    need = start + w * h * 2
    fill_until(ser, buf, lambda b: len(b) >= need, 20.0)
    data = bytes(buf[start:need])
    ser.close()

    rgb565_to_png(data, w, h, args.out)
    print(f"saved {args.out} ({w}x{h}) from {port}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
